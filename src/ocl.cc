#define OCL_CC_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#ifndef _MSC_VER
#include <alloca.h>
#else
#include <malloc.h>
#endif
#include <sys/stat.h>
#include "ocl.h"
#include "ocl_errstr.h"


class InitCL {
public:
	InitCL();
};

struct device_info {
	cl_device_id id;
	cl_device_type type;
	unsigned int units;
	unsigned int clock;

	unsigned int dim;
	size_t *work_item_sizes;
	size_t work_group_size;

	unsigned long mem_size;
};

static bool init_opencl(void);
static int select_device(struct device_info *di, int (*devcmp)(struct device_info*, struct device_info*));
static int get_dev_info(cl_device_id dev, struct device_info *di);
static int devcmp(struct device_info *a, struct device_info *b);
static const char *devtypestr(cl_device_type type);
static void print_memsize(FILE *out, unsigned long memsz);
static const char *clstrerror(int err);


static InitCL initcl;
static cl_context ctx;
static cl_command_queue cmdq;
static device_info devinf;

InitCL::InitCL()
{
	if(!init_opencl()) {
		exit(0);
	}
}

static bool init_opencl(void)
{
	if(select_device(&devinf, devcmp) == -1) {
		return false;
	}


	if(!(ctx = clCreateContext(0, 1, &devinf.id, 0, 0, 0))) {
		fprintf(stderr, "failed to create opencl context\n");
		return false;
	}

	if(!(cmdq = clCreateCommandQueue(ctx, devinf.id, 0, 0))) {
		fprintf(stderr, "failed to create command queue\n");
		return false;
	}
	return true;
}


CLMemBuffer *create_mem_buffer(int rdwr, size_t sz, void *buf)
{
	int err;
	cl_mem mem;
	cl_mem_flags flags = rdwr | CL_MEM_ALLOC_HOST_PTR;

	if(buf) {
		flags |= CL_MEM_COPY_HOST_PTR;
	}


	if(!(mem = clCreateBuffer(ctx, flags, sz, buf, &err))) {
		fprintf(stderr, "failed to create memory buffer: %s\n", clstrerror(err));
		return 0;
	}

	CLMemBuffer *mbuf = new CLMemBuffer;
	mbuf->mem = mem;
	mbuf->size = sz;
	mbuf->ptr = 0;
	return mbuf;
}

void destroy_mem_buffer(CLMemBuffer *mbuf)
{
	if(mbuf) {
		clReleaseMemObject(mbuf->mem);
		delete mbuf;
	}
}

void *map_mem_buffer(CLMemBuffer *mbuf, int rdwr)
{
	if(!mbuf) return 0;

#ifndef NDEBUG
	if(mbuf->ptr) {
		fprintf(stderr, "WARNING: map_mem_buffer called on already mapped buffer\n");
	}
#endif

	int err;
	mbuf->ptr = clEnqueueMapBuffer(cmdq, mbuf->mem, 1, rdwr, 0, mbuf->size, 0, 0, 0, &err);
	if(!mbuf->ptr) {
		fprintf(stderr, "failed to map buffer: %s\n", clstrerror(err));
		return 0;
	}
	return mbuf->ptr;
}

void unmap_mem_buffer(CLMemBuffer *mbuf)
{
	if(!mbuf || !mbuf->ptr) return;
	clEnqueueUnmapMemObject(cmdq, mbuf->mem, mbuf->ptr, 0, 0, 0);
	mbuf->ptr = 0;
}

bool write_mem_buffer(CLMemBuffer *mbuf, size_t sz, void *src)
{
	if(!mbuf) return false;

	int err;
	if((err = clEnqueueWriteBuffer(cmdq, mbuf->mem, 1, 0, sz, src, 0, 0, 0)) != 0) {
		fprintf(stderr, "failed to write buffer: %s\n", clstrerror(err));
		return false;
	}
	return true;
}

bool read_mem_buffer(CLMemBuffer *mbuf, size_t sz, void *dest)
{
	if(!mbuf) return false;

	int err;
	if((err = clEnqueueReadBuffer(cmdq, mbuf->mem, 1, 0, sz, dest, 0, 0, 0)) != 0) {
		fprintf(stderr, "failed to read buffer: %s\n", clstrerror(err));
		return false;
	}
	return true;
}


CLProgram::CLProgram(const char *kname)
{
	prog = 0;
	kernel = 0;
	this->kname = kname;
	args.resize(16);
	built = false;
}

CLProgram::~CLProgram()
{
	if(prog) {

		clReleaseProgram(prog);
	}
	if(kernel) {

		clReleaseKernel(kernel);
	}
	for(size_t i=0; i<args.size(); i++) {
		if(args[i].type == ARGTYPE_MEM_BUF) {
			destroy_mem_buffer(args[i].v.mbuf);
		}
	}
}

bool CLProgram::load(const char *fname)
{
	FILE *fp;
	char *src;
	struct stat st;

	printf("loading opencl program (%s)\n", fname);

	if(!(fp = fopen(fname, "rb"))) {
		fprintf(stderr, "failed to open %s: %s\n", fname, strerror(errno));
		return false;
	}

	fstat(fileno(fp), &st);

	src = new char[st.st_size + 1];

	fread(src, 1, st.st_size, fp);
	src[st.st_size] = 0;
	fclose(fp);


	if(!(prog = clCreateProgramWithSource(ctx, 1, (const char**)&src, 0, 0))) {
		fprintf(stderr, "error creating program object: %s\n", fname);
		delete [] src;
		return false;
	}
	delete [] src;
	return true;
}

bool CLProgram::set_argi(int idx, int val)
{
	if((int)args.size() <= idx) {
		args.resize(idx + 1);
	}

	CLArg *arg = &args[idx];
	arg->type = ARGTYPE_INT;
	arg->v.ival = val;
	return true;
}

bool CLProgram::set_argf(int idx, float val)
{
	if((int)args.size() <= idx) {
		args.resize(idx + 1);
	}

	CLArg *arg = &args[idx];
	arg->type = ARGTYPE_FLOAT;
	arg->v.fval = val;
	return true;
}

bool CLProgram::set_arg_buffer(int idx, int rdwr, size_t sz, void *ptr)
{
	CLMemBuffer *buf;

	if(!(buf = create_mem_buffer(rdwr, sz, ptr))) {
		return false;
	}

	if((int)args.size() <= idx) {
		args.resize(idx + 1);
	}
	args[idx].type = ARGTYPE_MEM_BUF;
	args[idx].v.mbuf = buf;
	return true;
}

CLMemBuffer *CLProgram::get_arg_buffer(int arg)
{
	if(arg < 0 || arg >= (int)args.size() || args[arg].type != ARGTYPE_MEM_BUF) {
		return 0;
	}
	return args[arg].v.mbuf;
}

bool CLProgram::build()
{
	int err;

	if((err = clBuildProgram(prog, 0, 0, 0, 0, 0)) != 0) {
		size_t sz;
		clGetProgramBuildInfo(prog, devinf.id, CL_PROGRAM_BUILD_LOG, 0, 0, &sz);

		char *errlog = (char*)alloca(sz + 1);
		clGetProgramBuildInfo(prog, devinf.id, CL_PROGRAM_BUILD_LOG, sz, errlog, 0);
		fprintf(stderr, "failed to build program: %s\n%s\n", clstrerror(err), errlog);

		clReleaseProgram(prog);
		prog = 0;
		return false;
	}


	if(!(kernel = clCreateKernel(prog, kname.c_str(), 0))) {
		fprintf(stderr, "failed to create kernel: %s\n", kname.c_str());
		clReleaseProgram(prog);
		prog = 0;
		return false;
	}

	for(size_t i=0; i<args.size(); i++) {
		int err;

		if(args[i].type == ARGTYPE_NONE) {
			break;
		}

		switch(args[i].type) {
		case ARGTYPE_INT:
			if((err = clSetKernelArg(kernel, i, sizeof(int), &args[i].v.ival)) != 0) {
				fprintf(stderr, "failed to bind kernel argument %d: %s\n", (int)i, clstrerror(err));
				goto fail;
			}
			break;

		case ARGTYPE_FLOAT:
			if((err = clSetKernelArg(kernel, i, sizeof(float), &args[i].v.fval)) != 0) {
				fprintf(stderr, "failed to bind kernel argument %d: %s\n", (int)i, clstrerror(err));
				goto fail;
			}
			break;

		case ARGTYPE_MEM_BUF:
			{
				CLMemBuffer *mbuf = args[i].v.mbuf;

				if((err = clSetKernelArg(kernel, i, sizeof mbuf->mem, &mbuf->mem)) != 0) {
					fprintf(stderr, "failed to bind kernel argument %d: %s\n", (int)i, clstrerror(err));
					goto fail;
				}
			}
			break;

		default:
			break;
		}
	}

	built = true;
	return true;

fail:
	clReleaseProgram(prog);
	clReleaseKernel(kernel);
	prog = 0;
	kernel = 0;
	return false;
}

bool CLProgram::run() const
{
	return run(1, 1);
}

bool CLProgram::run(int dim, ...) const
{
	if(!built) {
		if(!((CLProgram*)this)->build()) {
			return false;
		}
	}

	va_list ap;
	size_t *global_size = (size_t*)alloca(dim * sizeof *global_size);

	va_start(ap, dim);
	for(int i=0; i<dim; i++) {
		global_size[i] = va_arg(ap, int);
	}
	va_end(ap);

	int err;
	if((err = clEnqueueNDRangeKernel(cmdq, kernel, dim, 0, global_size, 0, 0, 0, 0)) != 0) {
		fprintf(stderr, "error executing kernel: %s\n", clstrerror(err));
		return false;
	}
	return true;
}

static int select_device(struct device_info *dev_inf, int (*devcmp)(struct device_info*, struct device_info*))
{
	unsigned int i, j, num_dev, num_plat, sel, ret;
	cl_device_id dev[32];
	cl_platform_id plat[32];

	dev_inf->work_item_sizes = 0;

	if((ret = clGetPlatformIDs(32, plat, &num_plat)) != 0) {
		fprintf(stderr, "clGetPlatformIDs failed: %s\n", clstrerror(ret));
		return -1;
	}
	if(!num_plat) {
		fprintf(stderr, "OpenCL not available!\n");
		return -1;
	}

	for(i=0; i<num_plat; i++) {
		char buf[512];

		clGetPlatformInfo(plat[i], CL_PLATFORM_NAME, sizeof buf, buf, 0);
		printf("[%d]: %s", i, buf);
		clGetPlatformInfo(plat[i], CL_PLATFORM_VENDOR, sizeof buf, buf, 0);
		printf(", %s", buf);
		clGetPlatformInfo(plat[i], CL_PLATFORM_VERSION, sizeof buf, buf, 0);
		printf(" (%s)\n", buf);
	}

	if((ret = clGetDeviceIDs(plat[0], CL_DEVICE_TYPE_ALL, 32, dev, &num_dev)) != 0) {
		fprintf(stderr, "clGetDeviceIDs failed: %s\n", clstrerror(ret));
		return -1;
	}
	printf("found %d cl devices.\n", num_dev);

	for(i=0; i<num_dev; i++) {
		struct device_info di;

		if(get_dev_info(dev[i], &di) == -1) {
			free(dev_inf->work_item_sizes);
			return -1;
		}

		printf("--> device %u (%s)\n", i, devtypestr(di.type));
		printf("max compute units: %u\n", di.units);
		printf("max clock frequency: %u\n", di.clock);
		printf("max work item dimensions: %u\n", di.dim);

		printf("max work item sizes: ");
		for(j=0; j<di.dim; j++) {
			printf("%u", (unsigned int)di.work_item_sizes[j]);
			if(di.dim - j > 1) {
				printf(", ");
			}
		}
		putchar('\n');

		printf("max work group size: %u\n", (unsigned int)di.work_group_size);
		printf("max object allocation size: ");
		print_memsize(stdout, di.mem_size);
		putchar('\n');

		if(devcmp(&di, dev_inf) > 0) {
			free(dev_inf->work_item_sizes);
			memcpy(dev_inf, &di, sizeof di);
			sel = i;
		}
	}

	if(num_dev) {
		printf("\nusing device: %d\n", sel);
		return 0;
	}

	return -1;
}

static int get_dev_info(cl_device_id dev, struct device_info *di)
{
	di->id = dev;


	clGetDeviceInfo(dev, CL_DEVICE_TYPE, sizeof di->type, &di->type, 0);
	clGetDeviceInfo(dev, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof di->units, &di->units, 0);
	clGetDeviceInfo(dev, CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof di->clock, &di->clock, 0);
	clGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS, sizeof di->dim, &di->dim, 0);

	di->work_item_sizes = new size_t[di->dim];

	clGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_ITEM_SIZES, di->dim * sizeof *di->work_item_sizes, di->work_item_sizes, 0);
	clGetDeviceInfo(dev, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof di->work_group_size, &di->work_group_size, 0);
	clGetDeviceInfo(dev, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof di->mem_size, &di->mem_size, 0);

	return 0;
}

static int devcmp(struct device_info *a, struct device_info *b)
{
	unsigned int aval = a->units * a->clock;
	unsigned int bval = b->units * b->clock;

	return aval - bval;
}

static const char *devtypestr(cl_device_type type)
{
	switch(type) {
	case CL_DEVICE_TYPE_CPU:
		return "cpu";
	case CL_DEVICE_TYPE_GPU:
		return "gpu";
	case CL_DEVICE_TYPE_ACCELERATOR:
		return "accelerator";
	default:
		break;
	}
	return "unknown";
}

static void print_memsize(FILE *out, unsigned long bytes)
{
	int i;
	unsigned long memsz = bytes;
	const char *suffix[] = {"bytes", "kb", "mb", "gb", "tb", "pb", 0};

	for(i=0; suffix[i]; i++) {
		if(memsz < 1024) {
			fprintf(out, "%lu %s", memsz, suffix[i]);
			if(i > 0) {
				fprintf(out, " (%lu bytes)", bytes);
			}
			return;
		}

		memsz /= 1024;
	}
}

static const char *clstrerror(int err)
{
	if(err > 0) {
		return "<invalid error code>";
	}
	if(err <= -(int)(sizeof ocl_errstr / sizeof *ocl_errstr)) {
		return "<unknown error>";
	}
	return ocl_errstr[-err];
}
