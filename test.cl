__kernel void test(__global const int *src, __global int *dst)
{
	int idx = get_global_id(0);

	dst[idx] = src[idx] * 2.0;
}
