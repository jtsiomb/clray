#ifndef OCL_H_
#define OCL_H_

#include <vector>
#include <string>
#ifndef __APPLE__
#include <CL/cl.h>
#include <CL/cl_gl.h>
#else
#include <OpenCL/opencl.h>
#endif

enum {
	ARG_RD		= CL_MEM_READ_ONLY,
	ARG_WR		= CL_MEM_WRITE_ONLY,
	ARG_RDWR	= CL_MEM_READ_WRITE
};

enum {
	MAP_RD		= CL_MAP_READ,
	MAP_WR		= CL_MAP_WRITE,
	MAP_RDWR	= CL_MAP_READ | CL_MAP_WRITE
};

enum {
	MEM_BUFFER,
	IMAGE_BUFFER
};

struct CLMemBuffer {
	int type;
	cl_mem mem;

	size_t size;
	size_t xsz, ysz;
	void *ptr;
	unsigned int tex;
};


bool init_opencl();
void destroy_opencl();

void finish_opencl();

CLMemBuffer *create_mem_buffer(int rdwr, size_t sz, const void *buf);

CLMemBuffer *create_image_buffer(int rdwr, int xsz, int ysz, const void *pixels = 0);
CLMemBuffer *create_image_buffer(int rdwr, unsigned int tex);

void destroy_mem_buffer(CLMemBuffer *mbuf);

void *map_mem_buffer(CLMemBuffer *mbuf, int rdwr, cl_event *ev = 0);
void unmap_mem_buffer(CLMemBuffer *mbuf, cl_event *ev = 0);

bool write_mem_buffer(CLMemBuffer *mbuf, size_t sz, const void *src, cl_event *ev = 0);
bool read_mem_buffer(CLMemBuffer *mbuf, size_t sz, void *dest, cl_event *ev = 0);

bool acquire_gl_object(CLMemBuffer *mbuf, cl_event *ev = 0);
bool release_gl_object(CLMemBuffer *mbuf, cl_event *ev = 0);

enum {
	ARGTYPE_NONE,

	ARGTYPE_INT,
	ARGTYPE_FLOAT,
	ARGTYPE_FLOAT4,
	ARGTYPE_MEM_BUF
};

struct CLArg {
	int type;
	union {
		int ival;
		float fval;
		cl_float4 vval;
		CLMemBuffer *mbuf;
	} v;

	CLArg();
};


class CLProgram {
private:
	std::string kname;
	cl_program prog;
	cl_kernel kernel;
	std::vector<CLArg> args;
	bool built;
	mutable cl_event wait_event;
	mutable cl_event last_event;

public:
	CLProgram(const char *kname);
	~CLProgram();

	bool load(const char *fname);

	bool set_argi(int arg, int val);
	bool set_argf(int arg, float val);
	bool set_arg_buffer(int arg, int rdwr, size_t sz, const void *buf = 0);
	bool set_arg_image(int arg, int rdwr, int xsz, int ysz, const void *pix = 0);
	bool set_arg_texture(int arg, int rdwr, unsigned int tex);
	CLMemBuffer *get_arg_buffer(int arg);
	int get_num_args() const;

	bool build(const char *opt = 0);

	bool run() const;
	bool run(int dim, ...) const;

	// sets an event that has to be completed before running the kernel
	void set_wait_event(cl_event ev);

	// gets the last event so that we can wait for it to finish
	cl_event get_last_event() const;
};

#endif	/* OCL_H_ */
