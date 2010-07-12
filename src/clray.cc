#include <stdio.h>
#include <assert.h>
#include "ocl.h"

struct Sphere {
	cl_float4 pos;
	cl_float radius;

	cl_float4 color;
} __attribute__((packed));

struct Ray {
	cl_float4 origin, dir;
} __attribute__((packed));


int main()
{
	int data[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
	int res[16];
	int count = sizeof data / sizeof *data;

	for(int i=0; i<count; i++) {
		printf("%d ", data[i]);
	}
	putchar('\n');

	CLProgram prog("test");
	if(!prog.load("test.cl")) {
		return 1;
	}
	if(!prog.set_arg_buffer(0, ARG_WR, sizeof res, res)) {
		return 1;
	}
	if(!prog.set_arg_buffer(1, ARG_RD, sizeof data, data)) {
		return 1;
	}
	if(!prog.set_argi(2, 100)) {
		return 1;
	}

	if(!prog.run(1, 16)) {
		return 1;
	}

	CLMemBuffer *mbuf = prog.get_arg_buffer(1);
	map_mem_buffer(mbuf, MAP_RD);

	for(int i=0; i<count; i++) {
		printf("%d ", res[i]);
	}
	putchar('\n');
	unmap_mem_buffer(mbuf);

	return 0;
}
