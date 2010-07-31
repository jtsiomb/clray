#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "ocl.h"
#include "mesh.h"

#ifdef __GNUC__
#define PACKED	__attribute__((packed))
#else
#define PACKED
#endif

#ifdef _MSC_VER
#pragma push(pack, 1)
#endif

struct RendInfo {
	int xsz, ysz;
	int num_faces, num_lights;
	int max_iter;
} PACKED;

struct Ray {
	float origin[4], dir[4];
} PACKED;

struct Light {
	float pos[4], color[4];
} PACKED;

#ifdef _MSC_VER
#pragma pop(pack)
#endif

static Ray get_primary_ray(int x, int y, int w, int h, float vfov_deg);

static Ray *prim_rays;
static CLProgram *prog;
static int global_size;

static Face faces[] = {
	{/* face0 */
		{
			{{-1, 0, 0, 1}, {0, 0, -1, 1}, {0, 0}},
			{{0, 1, 0, 1}, {0, 0, -1, 1}, {0, 0}},
			{{1, 0, 0, 1}, {0, 0, -1, 1}, {0, 0}}
		},
		{0, 0, -1, 1}, 0
	},
	{/* face1 */
		{
			{{-5, 0, -3, 1}, {0, 0, -1, 1}, {0, 0}},
			{{0, 0, 3, 1}, {0, 0, -1, 1}, {0, 0}},
			{{5, 0, -3, 1}, {0, 0, -1, 1}, {0, 0}}
		},
		{0, 0, -1, 1}, 1
	}
};

static Material matlib[] = {
	{{1, 0, 0, 1}, {1, 1, 1, 1}, 0, 0, 60.0},
	{{0.2, 0.8, 0.3, 1}, {0, 0, 0, 0}, 0, 0, 0}
};

static Light lightlist[] = {
	{{-10, 10, -20, 1}, {1, 1, 1, 1}}
};

static float xform[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
};

static RendInfo rinf;


bool init_renderer(int xsz, int ysz, float *fb)
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
	prog->set_arg_buffer(0, ARG_WR, xsz * ysz * 4 * sizeof(float), fb);
	prog->set_arg_buffer(1, ARG_RD, sizeof rinf, &rinf);
	prog->set_arg_buffer(2, ARG_RD, sizeof faces, faces);
	prog->set_arg_buffer(3, ARG_RD, sizeof matlib, matlib);
	prog->set_arg_buffer(4, ARG_RD, sizeof lightlist, lightlist);
	prog->set_arg_buffer(5, ARG_RD, xsz * ysz * sizeof *prim_rays, prim_rays);
	prog->set_arg_buffer(6, ARG_RD, sizeof xform, &xform);

	global_size = xsz * ysz;
	return true;
}

void destroy_renderer()
{
	delete [] prim_rays;
	delete prog;
}

bool render()
{
	if(!prog->run(1, global_size)) {
		return false;
	}

	CLMemBuffer *mbuf = prog->get_arg_buffer(0);
	map_mem_buffer(mbuf, MAP_RD);
	unmap_mem_buffer(mbuf);
	return true;
}

void set_xform(float *matrix)
{
	CLMemBuffer *mbuf = prog->get_arg_buffer(6);
	assert(mbuf);

	assert(map_mem_buffer(mbuf, MAP_WR) == xform);
	memcpy(xform, matrix, sizeof xform);
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

	px *= 100.0;
	py *= 100.0;
	pz *= 100.0;

	Ray ray = {{0, 0, 0, 1}, {px, py, pz, 1}};
	return ray;
}
