#include <stdio.h>
#include <math.h>
#include <assert.h>
#include "ocl.h"

struct RendInfo {
	int xsz, ysz;
	int num_sph, num_lights;
	int max_iter;
} __attribute__((packed));

struct Sphere {
	cl_float4 pos;
	cl_float radius;

	cl_float4 color;
} __attribute__((packed));

struct Ray {
	cl_float4 origin, dir;
} __attribute__((packed));

struct Light {
	cl_float4 pos, color;
} __attribute__((packed));


static Ray get_primary_ray(int x, int y, int w, int h, float vfov_deg);

static Ray *prim_rays;
static CLProgram *prog;
static int global_size;

bool init_renderer(int xsz, int ysz, float *fb)
{
	Sphere sphlist[] = {
		{{0, 0, 10, 1}, 1.0, {1, 0, 0, 1}}
	};
	Light lightlist[] = {
		{{-10, 10, -20, 1}, {1, 1, 1, 1}}
	};
	RendInfo rinf = {
		xsz, ysz,
		sizeof sphlist / sizeof *sphlist,
		sizeof lightlist / sizeof *lightlist,
		6
	};
	
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
		return 1;
	}

	/* setup argument buffers */
	prog->set_arg_buffer(0, ARG_WR, xsz * ysz * 4 * sizeof(float), fb);
	prog->set_arg_buffer(1, ARG_RD, sizeof rinf, &rinf);
	prog->set_arg_buffer(2, ARG_RD, sizeof sphlist, sphlist);
	prog->set_arg_buffer(3, ARG_RD, sizeof lightlist, lightlist);
	prog->set_arg_buffer(4, ARG_RD, xsz * ysz * sizeof *prim_rays, prim_rays);

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
	/*if(!write_ppm("out.ppm", fb, xsz, ysz)) {
		return 1;
	}*/
	unmap_mem_buffer(mbuf);
	return true;
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

	pz *= 1000.0;

	Ray ray = {{0, 0, 0, 1}, {px, py, pz, 1}};
	return ray;
}
