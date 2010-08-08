#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "ogl.h"
#include "ocl.h"
#include "mesh.h"

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
	KARG_OUTFACES,	/* DBG */

	NUM_KERNEL_ARGS
};

struct RendInfo {
	int xsz, ysz;
	int num_faces, num_lights;
	int max_iter;
	int dbg;
};

struct Ray {
	float origin[4], dir[4];
};

struct Light {
	float pos[4], color[4];
};

static Ray get_primary_ray(int x, int y, int w, int h, float vfov_deg);
static Face *create_face_buffer(Mesh **meshes, int num_meshes);

static Face *faces;
static Ray *prim_rays;
static CLProgram *prog;
static int global_size;

static Light lightlist[] = {
	{{-10, 10, -20, 0}, {1, 1, 1, 1}}
};


static RendInfo rinf;


bool init_renderer(int xsz, int ysz, Scene *scn)
{
	// render info
	rinf.xsz = xsz;
	rinf.ysz = ysz;
	rinf.num_faces = scn->get_num_faces();
	rinf.num_lights = sizeof lightlist / sizeof *lightlist;
	rinf.max_iter = 6;
	rinf.dbg = 8;

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

	/*Face **/faces = create_face_buffer(&scn->meshes[0], scn->meshes.size());
	if(!faces) {
		fprintf(stderr, "failed to create face buffer\n");
		return false;
	}

	/* setup argument buffers */
	prog->set_arg_buffer(KARG_FRAMEBUFFER, ARG_WR, xsz * ysz * 4 * sizeof(float));
	prog->set_arg_buffer(KARG_RENDER_INFO, ARG_RD, sizeof rinf, &rinf);
	prog->set_arg_buffer(KARG_FACES, ARG_RD, rinf.num_faces * sizeof(Face), faces);
	prog->set_arg_buffer(KARG_MATLIB, ARG_RD, scn->get_num_materials() * sizeof(Material), scn->get_materials());
	prog->set_arg_buffer(KARG_LIGHTS, ARG_RD, sizeof lightlist, lightlist);
	prog->set_arg_buffer(KARG_PRIM_RAYS, ARG_RD, xsz * ysz * sizeof *prim_rays, prim_rays);
	prog->set_arg_buffer(KARG_XFORM, ARG_RD, 16 * sizeof(float));
	prog->set_arg_buffer(KARG_INVTRANS_XFORM, ARG_RD, 16 * sizeof(float));
	prog->set_arg_buffer(KARG_OUTFACES, ARG_WR, rinf.num_faces * sizeof(Face));

	if(prog->get_num_args() < NUM_KERNEL_ARGS) {
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
	printf("Running kernel...");
	fflush(stdout);
	if(!prog->run(1, global_size)) {
		return false;
	}
	printf("done\n");

	/* DEBUG */
	CLMemBuffer *dbgbuf = prog->get_arg_buffer(KARG_OUTFACES);
	Face *outfaces = (Face*)map_mem_buffer(dbgbuf, MAP_RD);
	for(int i=0; i<rinf.num_faces; i++) {
		if(!(faces[i] == outfaces[i])) {
			fprintf(stderr, "SKATA %d\n", i);
			return false;
		}
		faces[i] = outfaces[i];
	}
	printf("equality test passed\n");
	unmap_mem_buffer(dbgbuf);


	CLMemBuffer *mbuf = prog->get_arg_buffer(KARG_FRAMEBUFFER);
	void *fb = map_mem_buffer(mbuf, MAP_RD);
	if(!fb) {
		fprintf(stderr, "FAILED\n");
		return false;
	}

	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rinf.xsz, rinf.ysz, GL_RGBA, GL_FLOAT, fb);
	unmap_mem_buffer(mbuf);
	return true;
}

void dbg_set_dbg(int dbg)
{
	printf("setting dbg: %d\n", dbg);

	CLMemBuffer *mbuf = prog->get_arg_buffer(KARG_RENDER_INFO);
	RendInfo *rinf = (RendInfo*)map_mem_buffer(mbuf, MAP_WR);
	rinf->dbg = dbg;
	unmap_mem_buffer(mbuf);
}

void dbg_render_gl(Scene *scn)
{
	float lpos[] = {-1, 1, 10, 0};
	glPushAttrib(GL_ENABLE_BIT | GL_TRANSFORM_BIT);

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);
	glEnable(GL_LIGHT0);
	glLightfv(GL_LIGHT0, GL_POSITION, lpos);
	glEnable(GL_COLOR_MATERIAL);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	gluPerspective(45.0, (float)rinf.xsz / (float)rinf.ysz, 0.5, 1000.0);

	Material *materials = scn->get_materials();

	glBegin(GL_TRIANGLES);
	int num_faces = scn->get_num_faces();
	for(int i=0; i<num_faces; i++) {
		Material *mat = materials ? materials + faces[i].matid : 0;

		if(mat) {
			glColor3f(mat->kd[0], mat->kd[1], mat->kd[2]);
		} else {
			glColor3f(1, 1, 1);
		}

		for(int j=0; j<3; j++) {
			float *pos = faces[i].v[j].pos;
			float *norm = faces[i].normal;
			glNormal3fv(norm);
			glVertex3fv(pos);
		}
	}

	/*for(size_t i=0; i<scn->meshes.size(); i++) {
		Material *mat = &scn->matlib[scn->meshes[i]->matid];

		glColor3f(mat->kd[0], mat->kd[1], mat->kd[2]);
		for(size_t j=0; j<scn->meshes[i]->faces.size(); j++) {
			for(int k=0; k<3; k++) {
				float *pos = scn->meshes[i]->faces[j].v[k].pos;
				glVertex3f(pos[0], pos[1], pos[2]);
			}
		}
	}*/
	glEnd();

	glPopMatrix();
	glPopAttrib();
}

void set_xform(float *matrix, float *invtrans)
{
	CLMemBuffer *mbuf_xform = prog->get_arg_buffer(KARG_XFORM);
	CLMemBuffer *mbuf_invtrans = prog->get_arg_buffer(KARG_INVTRANS_XFORM);
	assert(mbuf_xform && mbuf_invtrans);

	float *mem = (float*)map_mem_buffer(mbuf_xform, MAP_WR);
	memcpy(mem, matrix, 16 * sizeof *mem);
	/*printf("-- xform:\n");
	for(int i=0; i<16; i++) {
		printf("%2.3f\t", mem[i]);
		if(i % 4 == 3) putchar('\n');
	}*/
	unmap_mem_buffer(mbuf_xform);

	mem = (float*)map_mem_buffer(mbuf_invtrans, MAP_WR);
	memcpy(mem, invtrans, 16 * sizeof *mem);
	/*printf("-- inverse-transpose:\n");
	for(int i=0; i<16; i++) {
		printf("%2.3f\t", mem[i]);
		if(i % 4 == 3) putchar('\n');
	}*/
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

static Face *create_face_buffer(Mesh **meshes, int num_meshes)
{
	int num_faces = 0;
	for(int i=0; i<num_meshes; i++) {
		num_faces += meshes[i]->faces.size();
	}
	printf("constructing face buffer with %d faces (out of %d meshes)\n", num_faces, num_meshes);

	Face *faces = new Face[num_faces];
	memset(faces, 0, num_faces * sizeof *faces);
	Face *fptr = faces;

	for(int i=0; i<num_meshes; i++) {
		for(size_t j=0; j<meshes[i]->faces.size(); j++) {
			*fptr++ = meshes[i]->faces[j];
		}
	}
	return faces;
}
