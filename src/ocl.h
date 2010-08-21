#ifndef OCL_H_
#define OCL_H_

#include <vector>
#include <string>
#ifndef __APPLE__
#include <CL/cl.h>
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

struct CLMemBuffer {
	cl_mem mem;
	size_t size;
	void *ptr;
};

CLMemBuffer *create_mem_buffer(int rdwr, size_t sz, const void *buf = 0);
void destroy_mem_buffer(CLMemBuffer *mbuf);

void *map_mem_buffer(CLMemBuffer *mbuf, int rdwr);
void unmap_mem_buffer(CLMemBuffer *mbuf);

bool write_mem_buffer(CLMemBuffer *mbuf, size_t sz, const void *src);
bool read_mem_buffer(CLMemBuffer *mbuf, size_t sz, void *dest);

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

public:
	CLProgram(const char *kname);
	~CLProgram();

	bool load(const char *fname);

	bool set_argi(int arg, int val);
	bool set_argf(int arg, float val);
	bool set_arg_buffer(int arg, int rdwr, size_t sz, const void *buf = 0);
	CLMemBuffer *get_arg_buffer(int arg);
	int get_num_args() const;

	bool build();

	bool run() const;
	bool run(int dim, ...) const;
};

#endif	/* OCL_H_ */
