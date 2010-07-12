struct RendInfo {
	int xsz, ysz;
	int num_sph;
	int max_iter;
};

struct Sphere {
	float4 pos;
	float radius;
	float4 color;
};

struct Ray {
	float4 origin, dir;
};

struct SurfPoint {
	float t;
	float3 pos, norm;
};

#define EPSILON 1e-6

bool intersect(struct Ray ray, __global const struct Sphere *sph, struct SurfPoint *sp);

__kernel void render(__global float4 *fb,
		__global const struct RendInfo *rinf,
		__global const struct Sphere *sphlist,
		__global const struct Ray *primrays)
{
	int idx = get_global_id(0);

	struct Ray ray = primrays[idx];
	struct SurfPoint sp;

	if(intersect(ray, sphlist, &sp)) {
		fb[idx] = (float4)(1, 0, 0, 1);
	} else {
		fb[idx] = (float4)(0, 0, 0, 1);
	}
}

bool intersect(struct Ray ray,
		__global const struct Sphere *sph,
		struct SurfPoint *sp)
{
	float3 dir = ray.dir.xyz;
	float3 orig = ray.origin.xyz;
	float3 spos = sph->pos.xyz;

	float a = dot(dir, dir);
	float b = 2.0 * dir.x * (orig.x - spos.x) +
		2.0 * dir.y * (orig.y - spos.y) +
		2.0 * dir.z * (orig.z - spos.z);
	float c = dot(spos, spos) + dot(orig, orig) +
		2.0 * dot(-spos, orig) - sph->radius * sph->radius;

	float d = b * b - 4.0 * a * c;
	if(d < 0.0) return false;

	float sqrt_d = sqrt(d);
	float t1 = (-b + sqrt_d) / (2.0 * a);
	float t2 = (-b - sqrt_d) / (2.0 * a);

	if(t1 < EPSILON) t1 = t2;
	if(t2 < EPSILON) t2 = t1;
	float t = t1 < t2 ? t1 : t2;

	if(t < EPSILON || t > 1.0) {
		return false;
	}

	sp->t = t;
	sp->pos = orig + dir * sp->t;
	sp->norm = (sp->pos - spos) / sph->radius;
	return true;
}
