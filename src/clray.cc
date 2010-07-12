#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <assert.h>
#include "ocl.h"

struct RendInfo {
	int xsz, ysz;
	int num_sph;
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


Ray get_primary_ray(int x, int y, int w, int h, float vfov_deg);
bool write_ppm(const char *fname, float *fb, int xsz, int ysz);

int main()
{
	const int xsz = 800;
	const int ysz = 600;

	Sphere sphlist[] = {
		{{0, 0, 10, 1}, 1.0, {1, 0, 0, 1}}
	};
	RendInfo rinf = {xsz, ysz, sizeof sphlist / sizeof *sphlist, 6};
	Ray *prim_rays = new Ray[xsz * ysz];
	float *fb = new float[xsz * ysz * 4];

	/* calculate primary rays */
	for(int i=0; i<ysz; i++) {
		for(int j=0; j<xsz; j++) {
			prim_rays[i * xsz + j] = get_primary_ray(j, i, xsz, ysz, 45.0);
		}
	}

	/* setup opencl */
	CLProgram prog("render");
	if(!prog.load("rt.cl")) {
		return 1;
	}

	prog.set_arg_buffer(0, ARG_WR, xsz * ysz * 4 * sizeof(float), fb);
	prog.set_arg_buffer(1, ARG_RD, sizeof rinf, &rinf);
	prog.set_arg_buffer(2, ARG_RD, sizeof sphlist, sphlist);
	prog.set_arg_buffer(3, ARG_RD, xsz * ysz * sizeof *prim_rays, prim_rays);

	if(!prog.run(1, xsz * ysz)) {
		return 1;
	}

	CLMemBuffer *mbuf = prog.get_arg_buffer(0);
	map_mem_buffer(mbuf, MAP_RD);
	if(!write_ppm("out.ppm", fb, xsz, ysz)) {
		return 1;
	}
	unmap_mem_buffer(mbuf);

	delete [] fb;
	delete [] prim_rays;

	return 0;
}

Ray get_primary_ray(int x, int y, int w, int h, float vfov_deg)
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

bool write_ppm(const char *fname, float *fb, int xsz, int ysz)
{
	FILE *fp;

	if(!(fp = fopen(fname, "wb"))) {
		fprintf(stderr, "write_ppm: failed to open file %s for writing: %s\n", fname, strerror(errno));
		return false;
	}
	fprintf(fp, "P6\n%d %d\n255\n", xsz, ysz);

	for(int i=0; i<xsz * ysz * 4; i++) {
		if(i % 4 == 3) continue;

		unsigned char c = (unsigned char)(fb[i] * 255.0);
		fputc(c, fp);
	}
	fclose(fp);
	return true;
}
