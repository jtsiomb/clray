__kernel void test(__global int *dst, __global const int *src, __global const int foo)
{
	int idx = get_global_id(0);

	dst[idx] = src[idx] * foo;
}
