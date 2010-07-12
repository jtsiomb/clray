#ifndef OCL_H_
#define OCL_H_

#include <vector>
#include <string>
#ifndef __APPLE__
#include <CL/opencl.h>
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

CLMemBuffer *create_mem_buffer(int rdwr, size_t sz, void *buf);
void destroy_mem_buffer(CLMemBuffer *mbuf);

void *map_mem_buffer(CLMemBuffer *mbuf, int rdwr);
void unmap_mem_buffer(CLMemBuffer *mbuf);

bool write_mem_buffer(CLMemBuffer *mbuf, size_t sz, void *src);
bool read_mem_buffer(CLMemBuffer *mbuf, size_t sz, void *dest);


class CLProgram {
private:
	std::string kname;
	cl_program prog;
	cl_kernel kernel;
	std::vector<CLMemBuffer*> mbuf;
	bool built;

public:
	CLProgram(const char *kname);
	~CLProgram();

	bool load(const char *fname);

	bool set_arg(int arg, int rdwr, size_t sz, void *buf);
	CLMemBuffer *get_arg_buffer(int arg);

	bool build();

	bool run() const;
	bool run(int dim, ...) const;
};

#endif	/* OCL_H_ */
