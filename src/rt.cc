#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#ifndef __APPLE__
#include <GL/gl.h>
#include <GL/glu.h>
#else
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#endif

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
	KARG_INVTRANS_XFORM
};

struct RendInfo {
	int xsz, ysz;
	int num_faces, num_lights;
	int max_iter;
};

struct Ray {
	float origin[4], dir[4];
};

struct Light {
	float pos[4], color[4];
};

static Ray get_primary_ray(int x, int y, int w, int h, float vfov_deg);

static Ray *prim_rays;
static CLProgram *prog;
static int global_size;

static Face faces[] = {
	{/* face0 */
		{
			{{-1, 0, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
			{{0, 1, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
			{{1, 0, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}
		},
		{0, 0, -1, 0}, 0, {0, 0, 0}
	},
	{/* face1 */
		{
			{{-5, 0, -3, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
			{{0, 0, 3, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}},
			{{5, 0, -3, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}}
		},
		{0, 1, 0, 0}, 1, {0, 0, 0}
	}
};

static Material matlib[] = {
	{{1, 0, 0, 1}, {1, 1, 1, 1}, 0, 0, 60.0, 0},
	{{0.2, 0.8, 0.3, 1}, {0, 0, 0, 0}, 0, 0, 0, 0}
};

static Light lightlist[] = {
	{{-10, 10, -20, 0}, {1, 1, 1, 1}}
};


static RendInfo rinf;


bool init_renderer(int xsz, int ysz)
{
	// render info
	rinf.xsz = xsz;
	rinf.ysz = ysz;
	rinf.num_faces = sizeof faces / sizeof *faces;
	rinf.num_lights = sizeof lightlist / sizeof *lightlist;
	rinf.max_iter = 6;

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

	/* setup argument buffers */
	prog->set_arg_buffer(KARG_FRAMEBUFFER, ARG_WR, xsz * ysz * 4 * sizeof(float));
	prog->set_arg_buffer(KARG_RENDER_INFO, ARG_RD, sizeof rinf, &rinf);
	prog->set_arg_buffer(KARG_FACES, ARG_RD, sizeof faces, faces);
	prog->set_arg_buffer(KARG_MATLIB, ARG_RD, sizeof matlib, matlib);
	prog->set_arg_buffer(KARG_LIGHTS, ARG_RD, sizeof lightlist, lightlist);
	prog->set_arg_buffer(KARG_PRIM_RAYS, ARG_RD, xsz * ysz * sizeof *prim_rays, prim_rays);
	prog->set_arg_buffer(KARG_XFORM, ARG_RD, 16 * sizeof(float));
	prog->set_arg_buffer(KARG_INVTRANS_XFORM, ARG_RD, 16 * sizeof(float));

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
	if(!prog->run(1, global_size)) {
		return false;
	}

	CLMemBuffer *mbuf = prog->get_arg_buffer(0);
	void *fb = map_mem_buffer(mbuf, MAP_RD);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rinf.xsz, rinf.ysz, GL_RGBA, GL_FLOAT, fb);
	unmap_mem_buffer(mbuf);
	return true;
}

void dbg_render_gl()
{
	glPushAttrib(GL_ENABLE_BIT | GL_TRANSFORM_BIT);

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	gluPerspective(45.0, (float)rinf.xsz / (float)rinf.ysz, 0.5, 1000.0);

	glBegin(GL_TRIANGLES);
	for(int i=0; i<rinf.num_faces; i++) {
		Material *mat = matlib + faces[i].matid;
		glColor3f(mat->kd[0], mat->kd[1], mat->kd[2]);

		for(int j=0; j<3; j++) {
			float *pos = faces[i].v[j].pos;
			glVertex3f(pos[0], pos[1], pos[2]);
		}
	}
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
	printf("-- xform:\n");
	for(int i=0; i<16; i++) {
		printf("%2.3f\t", mem[i]);
		if(i % 4 == 3) putchar('\n');
	}
	unmap_mem_buffer(mbuf_xform);

	mem = (float*)map_mem_buffer(mbuf_invtrans, MAP_WR);
	memcpy(mem, invtrans, 16 * sizeof *mem);
	printf("-- inverse-transpose:\n");
	for(int i=0; i<16; i++) {
		printf("%2.3f\t", mem[i]);
		if(i % 4 == 3) putchar('\n');
	}
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
