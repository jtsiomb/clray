#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "ogl.h"
#include "ocl.h"
#include "scene.h"
#include "timer.h"

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

struct RendInfo {
	float ambient[4];
	int xsz, ysz;
	int num_faces, num_lights;
	int max_iter;
	int kd_depth;
};

struct Ray {
	float origin[4], dir[4];
};

struct Light {
	float pos[4], color[4];
};

static Ray get_primary_ray(int x, int y, int w, int h, float vfov_deg);

static Face *faces;
static Ray *prim_rays;
static CLProgram *prog;
static int global_size;

static Light lightlist[] = {
	{{-8, 15, 18, 0}, {1, 1, 1, 1}}
};


static RendInfo rinf;


bool init_renderer(int xsz, int ysz, Scene *scn, unsigned int tex)
{
	// render info
	rinf.ambient[0] = rinf.ambient[1] = rinf.ambient[2] = 0.0;
	rinf.ambient[3] = 0.0;

	rinf.xsz = xsz;
	rinf.ysz = ysz;
	rinf.num_faces = scn->get_num_faces();
	rinf.num_lights = sizeof lightlist / sizeof *lightlist;
	rinf.max_iter = 6;
	rinf.kd_depth = kdtree_depth(scn->kdtree);

	/* calculate primary rays */
	prim_rays = new Ray[xsz * ysz];

	for(int i=0; i<ysz; i++) {
		for(int j=0; j<xsz; j++) {
			prim_rays[i * xsz + j] = get_primary_ray(j, i, xsz, ysz, 45.0);
		}
	}

	/* setup opencl */
	prog = new CLProgram("render");
	if(!prog->load("rt.cl")) {
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
	// XXX now we can actually destroy the original kdtree and keep only the GPU version

	/* setup argument buffers */
#ifdef CLGL_INTEROP
	prog->set_arg_texture(KARG_FRAMEBUFFER, ARG_WR, tex);
#else
	prog->set_arg_image(KARG_FRAMEBUFFER, ARG_WR, xsz, ysz);
#endif
	prog->set_arg_buffer(KARG_RENDER_INFO, ARG_RD, sizeof rinf, &rinf);
	prog->set_arg_buffer(KARG_FACES, ARG_RD, rinf.num_faces * sizeof(Face), faces);
	prog->set_arg_buffer(KARG_MATLIB, ARG_RD, scn->get_num_materials() * sizeof(Material), scn->get_materials());
	prog->set_arg_buffer(KARG_LIGHTS, ARG_RD, sizeof lightlist, lightlist);
	prog->set_arg_buffer(KARG_PRIM_RAYS, ARG_RD, xsz * ysz * sizeof *prim_rays, prim_rays);
	prog->set_arg_buffer(KARG_XFORM, ARG_RD, 16 * sizeof(float));
	prog->set_arg_buffer(KARG_INVTRANS_XFORM, ARG_RD, 16 * sizeof(float));
	prog->set_arg_buffer(KARG_KDTREE, ARG_RD, scn->get_num_kdnodes() * sizeof *kdbuf, kdbuf);

	if(prog->get_num_args() < NUM_KERNEL_ARGS) {
		return false;
	}

	if(!prog->build()) {
		return false;
	}

	delete [] prim_rays;

	global_size = xsz * ysz;
	return true;
}

void destroy_renderer()
{
	delete prog;
}

bool render()
{
	// XXX do we need to call glFinish ?

	long tm0 = get_msec();

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

	printf("rendered in %ld msec\n", get_msec() - tm0);
	return true;
}

#define MIN(a, b)	((a) < (b) ? (a) : (b))
static void dbg_set_gl_material(Material *mat)
{
	static Material def_mat = {{0.7, 0.7, 0.7, 1}, {0, 0, 0, 0}, 0, 0, 0};

	if(!mat) mat = &def_mat;

	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat->kd);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat->ks);
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, MIN(mat->spow, 128.0f));
}

void dbg_render_gl(Scene *scn, bool show_tree, bool show_obj)
{
	glPushAttrib(GL_ENABLE_BIT | GL_TRANSFORM_BIT | GL_LIGHTING_BIT);

	for(int i=0; i<rinf.num_lights; i++) {
		float lpos[4];

		memcpy(lpos, lightlist[i].pos, sizeof lpos);
		lpos[3] = 1.0;

		glLightfv(GL_LIGHT0 + i, GL_POSITION, lpos);
		glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, lightlist[i].color);
		glEnable(GL_LIGHT0 + i);
	}

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	gluPerspective(45.0, (float)rinf.xsz / (float)rinf.ysz, 0.5, 1000.0);

	if(show_obj) {
		Material *materials = scn->get_materials();

		int num_faces = scn->get_num_faces();
		int cur_mat = -1;

		for(int i=0; i<num_faces; i++) {
			if(faces[i].matid != cur_mat) {
				if(cur_mat != -1) {
					glEnd();
				}
				dbg_set_gl_material(materials ? materials + faces[i].matid : 0);
				cur_mat = faces[i].matid;
				glBegin(GL_TRIANGLES);
			}

			for(int j=0; j<3; j++) {
				glNormal3fv(faces[i].v[j].normal);
				glVertex3fv(faces[i].v[j].pos);
			}
		}
		glEnd();
	}

	if(show_tree) {
		scn->draw_kdtree();
	}

	glPopMatrix();
	glPopAttrib();

	assert(glGetError() == GL_NO_ERROR);
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

static Ray get_primary_ray(int x, int y, int w, int h, float vfov_deg)
{
	float vfov = M_PI * vfov_deg / 180.0;
	float aspect = (float)w / (float)h;

	float ysz = 2.0;
	float xsz = aspect * ysz;

	float px = ((float)x / (float)w) * xsz - xsz / 2.0;
	float py = 1.0 - ((float)y / (float)h) * ysz;
	float pz = 1.0 / tan(0.5 * vfov);

	px *= 100.0;
	py *= 100.0;
	pz *= 100.0;

	Ray ray = {{0, 0, 0, 1}, {px, py, -pz, 1}};
	return ray;
}
