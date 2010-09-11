#include <stdio.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <assert.h>
#include "rt.h"
#include "ogl.h"
#include "ocl.h"
#include "scene.h"
#include "timer.h"
#include "common.h"

// kernel arguments
enum {
	KARG_FRAMEBUFFER,
	KARG_RENDER_INFO,
	KARG_FACES,
	KARG_MATLIB,
	KARG_LIGHTS,
	KARG_PRIM_RAYS,
	KARG_XFORM,
	KARG_INVTRANS_XFORM,
	KARG_KDTREE,

	NUM_KERNEL_ARGS
};

static void update_render_info();
static Ray get_primary_ray(int x, int y, int w, int h, float vfov_deg);
static float *create_kdimage(const KDNodeGPU *kdtree, int num_nodes, int *xsz_ret, int *ysz_ret);

static Face *faces;
static Ray *prim_rays;
static CLProgram *prog;
static int global_size;


static RendInfo rinf;
static RenderStats rstat;
static int saved_iter_val;

static long timing_sample_sum;
static long num_timing_samples;


bool init_renderer(int xsz, int ysz, Scene *scn, unsigned int tex)
{
	// render info
	rinf.ambient[0] = rinf.ambient[1] = rinf.ambient[2] = 0.0;
	rinf.ambient[3] = 0.0;

	rinf.xsz = xsz;
	rinf.ysz = ysz;
	rinf.num_faces = scn->get_num_faces();
	rinf.num_lights = scn->get_num_lights();
	rinf.max_iter = saved_iter_val = 6;
	rinf.cast_shadows = true;

	/* calculate primary rays */
	prim_rays = new Ray[xsz * ysz];

	for(int i=0; i<ysz; i++) {
		for(int j=0; j<xsz; j++) {
			prim_rays[i * xsz + j] = get_primary_ray(j, i, xsz, ysz, 45.0);
		}
	}
	dbg_set_primary_rays(prim_rays);	// give them to the debug renderer

	/* setup opencl */
	prog = new CLProgram("render");
	if(!prog->load("src/rt.cl")) {
		return false;
	}

	if(!(faces = (Face*)scn->get_face_buffer())) {
		fprintf(stderr, "failed to create face buffer\n");
		return false;
	}

	const KDNodeGPU *kdbuf = scn->get_kdtree_buffer();
	if(!kdbuf) {
		fprintf(stderr, "failed to create kdtree buffer\n");
		return false;
	}

	int kdimg_xsz, kdimg_ysz;
	float *kdimg_pixels = create_kdimage(kdbuf, scn->get_num_kdnodes(), &kdimg_xsz, &kdimg_ysz);

	/* setup argument buffers */
#ifdef CLGL_INTEROP
	prog->set_arg_texture(KARG_FRAMEBUFFER, ARG_WR, tex);
#else
	prog->set_arg_image(KARG_FRAMEBUFFER, ARG_WR, xsz, ysz);
#endif
	prog->set_arg_buffer(KARG_RENDER_INFO, ARG_RD, sizeof rinf, &rinf);
	prog->set_arg_buffer(KARG_FACES, ARG_RD, rinf.num_faces * sizeof(Face), faces);
	prog->set_arg_buffer(KARG_MATLIB, ARG_RD, scn->get_num_materials() * sizeof(Material), scn->get_materials());
	prog->set_arg_buffer(KARG_LIGHTS, ARG_RD, scn->get_num_lights() * sizeof(Light), scn->get_lights());
	prog->set_arg_buffer(KARG_PRIM_RAYS, ARG_RD, xsz * ysz * sizeof *prim_rays, prim_rays);
	prog->set_arg_buffer(KARG_XFORM, ARG_RD, 16 * sizeof(float));
	prog->set_arg_buffer(KARG_INVTRANS_XFORM, ARG_RD, 16 * sizeof(float));
	//prog->set_arg_buffer(KARG_KDTREE, ARG_RD, scn->get_num_kdnodes() * sizeof *kdbuf, kdbuf);
	prog->set_arg_image(KARG_KDTREE, ARG_RD, kdimg_xsz, kdimg_ysz, kdimg_pixels);

	delete [] kdimg_pixels;


	if(prog->get_num_args() < NUM_KERNEL_ARGS) {
		return false;
	}

	const char *opt = "-Isrc -cl-mad-enable -cl-single-precision-constant -cl-fast-relaxed-math";
	if(!prog->build(opt)) {
		return false;
	}

	//delete [] prim_rays; now dbg_renderer handles them

	global_size = xsz * ysz;


	init_dbg_renderer(xsz, ysz, scn, tex);
	return true;
}

void destroy_renderer()
{
	delete prog;

	destroy_dbg_renderer();

	if(num_timing_samples) {
		printf("rendertime mean: %ld msec\n", timing_sample_sum / num_timing_samples);
	}
}

bool render()
{
	long tm0 = get_msec();

	// initialize render-stats
	memset(&rstat, 0, sizeof rstat);
	rstat.min_aabb_tests = rstat.min_triangle_tests = INT_MAX;
	rstat.max_aabb_tests = rstat.max_triangle_tests = 0;

#ifdef CLGL_INTEROP
	cl_event ev;
	CLMemBuffer *texbuf = prog->get_arg_buffer(KARG_FRAMEBUFFER);

	if(!acquire_gl_object(texbuf, &ev)) {
		return false;
	}

	// make sure that we will wait for the acquire to finish before running
	prog->set_wait_event(ev);
#endif

	if(!prog->run(1, global_size)) {
		return false;
	}

#ifdef CLGL_INTEROP
	if(!release_gl_object(texbuf, &ev)) {
		return false;
	}
	clWaitForEvents(1, &ev);
#endif

#ifndef CLGL_INTEROP
	/* if we don't compile in CL/GL interoperability support, we need
	 * to copy the output buffer to the OpenGL texture used to displaying
	 * the image.
	 */
	CLMemBuffer *mbuf = prog->get_arg_buffer(KARG_FRAMEBUFFER);
	void *fb = map_mem_buffer(mbuf, MAP_RD);
	if(!fb) {
		fprintf(stderr, "FAILED\n");
		return false;
	}

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rinf.xsz, rinf.ysz, GL_RGBA, GL_FLOAT, fb);
	unmap_mem_buffer(mbuf);
#endif

	rstat.render_time = get_msec() - tm0;

	timing_sample_sum += rstat.render_time;
	num_timing_samples++;

	return true;
}


void set_xform(float *matrix, float *invtrans)
{
	CLMemBuffer *mbuf_xform = prog->get_arg_buffer(KARG_XFORM);
	CLMemBuffer *mbuf_invtrans = prog->get_arg_buffer(KARG_INVTRANS_XFORM);
	assert(mbuf_xform && mbuf_invtrans);

	float *mem = (float*)map_mem_buffer(mbuf_xform, MAP_WR);
	memcpy(mem, matrix, 16 * sizeof *mem);
	unmap_mem_buffer(mbuf_xform);

	mem = (float*)map_mem_buffer(mbuf_invtrans, MAP_WR);
	memcpy(mem, invtrans, 16 * sizeof *mem);
	unmap_mem_buffer(mbuf_invtrans);
}


const RendInfo *get_render_info()
{
	return &rinf;
}

const RenderStats *get_render_stats()
{
	return &rstat;
}

void print_render_stats(FILE *fp)
{
	fprintf(fp, "-- render stats --\n");
	fprintf(fp, "> timing\n");
	fprintf(fp, "   render time (msec): %lu\n", rstat.render_time);
	fprintf(fp, "   tex update time (msec): %lu\n", rstat.tex_update_time);
	fprintf(fp, "> counters\n");
	fprintf(fp, "   AABB tests: %d\n", rstat.aabb_tests);
	fprintf(fp, "   AABB tests per ray (min/max/avg): %d/%d/%f\n",
			rstat.min_aabb_tests, rstat.max_aabb_tests, rstat.avg_aabb_tests);
	fprintf(fp, "   triangle tests: %d\n", rstat.triangle_tests);
	fprintf(fp, "   triangle tests per ray (min/max/avg): %d/%d/%f\n",
			rstat.min_triangle_tests, rstat.max_triangle_tests, rstat.avg_triangle_tests);
	fprintf(fp, "   rays cast: %dp %dr %ds (sum: %d)\n", rstat.prim_rays,
			rstat.refl_rays, rstat.shadow_rays, rstat.rays_cast);
	fprintf(fp, "   rays per second: %d\n", rstat.rays_per_sec);
	fprintf(fp, "   BRDF evaluations: %d\n", rstat.brdf_evals);
	fputc('\n', fp);
}

void set_render_option(int opt, bool val)
{
	switch(opt) {
	case ROPT_ITER:
	case ROPT_REFL:
		rinf.max_iter = val ? saved_iter_val : 0;
		break;

	case ROPT_SHAD:
		rinf.cast_shadows = val;
		break;

	default:
		return;
	}

	update_render_info();
}

void set_render_option(int opt, int val)
{
	switch(opt) {
	case ROPT_ITER:
		rinf.max_iter = saved_iter_val = val;
		break;

	case ROPT_SHAD:
		rinf.cast_shadows = val;
		break;

	case ROPT_REFL:
		rinf.max_iter = val ? saved_iter_val : 0;
		break;

	default:
		return;
	}

	update_render_info();
}

void set_render_option(int opt, float val)
{
	set_render_option(opt, (int)val);
}

bool get_render_option_bool(int opt)
{
	switch(opt) {
	case ROPT_ITER:
		return rinf.max_iter;
	case ROPT_SHAD:
		return rinf.cast_shadows;
	case ROPT_REFL:
		return rinf.max_iter == saved_iter_val;
	default:
		break;
	}
	return false;
}

int get_render_option_int(int opt)
{
	switch(opt) {
	case ROPT_ITER:
		return rinf.max_iter;
	case ROPT_SHAD:
		return rinf.cast_shadows ? 1 : 0;
	case ROPT_REFL:
		return rinf.max_iter == saved_iter_val ? 1 : 0;
	default:
		break;
	}
	return -1;
}

float get_render_option_float(int opt)
{
	return (float)get_render_option_int(opt);
}

static void update_render_info()
{
	if(!prog) {
		return;
	}

	CLMemBuffer *mbuf = prog->get_arg_buffer(KARG_RENDER_INFO);
	assert(mbuf);

	RendInfo *rinf_ptr = (RendInfo*)map_mem_buffer(mbuf, MAP_WR);
	*rinf_ptr = rinf;
	unmap_mem_buffer(mbuf);
}

static Ray get_primary_ray(int x, int y, int w, int h, float vfov_deg)
{
	float vfov = M_PI * vfov_deg / 180.0;
	float aspect = (float)w / (float)h;

	float ysz = 2.0;
	float xsz = aspect * ysz;

	float px = ((float)x / (float)w) * xsz - xsz / 2.0;
	float py = 1.0 - ((float)y / (float)h) * ysz;
	float pz = 1.0 / tan(0.5 * vfov);

	float mag = sqrt(px * px + py * py + pz * pz);

	px = px * RAY_MAG / mag;
	py = py * RAY_MAG / mag;
	pz = pz * RAY_MAG / mag;

	Ray ray = {{0, 0, 0, 1}, {px, py, -pz, 1}};
	return ray;
}

#define MIN(a, b)	((a) < (b) ? (a) : (b))

static float *create_kdimage(const KDNodeGPU *kdtree, int num_nodes, int *xsz_ret, int *ysz_ret)
{
	int ysz = MIN(num_nodes, KDIMG_MAX_HEIGHT);
	int columns = (num_nodes - 1) / KDIMG_MAX_HEIGHT + 1;
	int xsz = KDIMG_NODE_WIDTH * columns;

	printf("creating kdtree image %dx%d (%d nodes)\n", xsz, ysz, num_nodes);

	float *img = new float[4 * xsz * ysz];
	memset(img, 0, 4 * xsz * ysz * sizeof *img);

	for(int i=0; i<num_nodes; i++) {
		int x = KDIMG_NODE_WIDTH * (i / KDIMG_MAX_HEIGHT);
		int y = i % KDIMG_MAX_HEIGHT;

		float *ptr = img + (y * xsz + x) * 4;

		*ptr++ = kdtree[i].aabb.min[0];
		*ptr++ = kdtree[i].aabb.min[1];
		*ptr++ = kdtree[i].aabb.min[2];
		*ptr++ = 0.0;

		*ptr++ = kdtree[i].aabb.max[0];
		*ptr++ = kdtree[i].aabb.max[1];
		*ptr++ = kdtree[i].aabb.max[2];
		*ptr++ = 0.0;

		for(int j=0; j<MAX_NODE_FACES; j++) {
			*ptr++ = j < kdtree[i].num_faces ? (float)kdtree[i].face_idx[j] : 0.0f;
		}

		*ptr++ = (float)kdtree[i].num_faces;
		*ptr++ = (float)kdtree[i].left;
		*ptr++ = (float)kdtree[i].right;
		*ptr++ = 0.0;
	}

	if(xsz_ret) *xsz_ret = xsz;
	if(ysz_ret) *ysz_ret = ysz;
	return img;
}
