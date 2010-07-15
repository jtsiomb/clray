struct RendInfo {
	int xsz, ysz;
	int num_sph, num_lights;
	int max_iter;
};

struct Sphere {
	float4 pos;
	float radius;
	float4 kd, ks;
	float spow, kr, kt;
};

struct Light {
	float4 pos, color;
};

struct Ray {
	float4 origin, dir;
};

struct SurfPoint {
	float t;
	float3 pos, norm;
	global const struct Sphere *obj;
};

#define EPSILON 1e-6

float4 shade(struct Ray ray, struct SurfPoint sp);
bool intersect(struct Ray ray, global const struct Sphere *sph, struct SurfPoint *sp);


kernel void render(global float4 *fb,
		global const struct RendInfo *rinf,
		global const struct Sphere *sphlist,
		global const struct Light *lights,
		global const struct Ray *primrays)
{
	int idx = get_global_id(0);

	struct Ray ray = primrays[idx];
	struct SurfPoint sp, sp0;

	sp0.t = FLT_MAX;

	for(int i=0; i<rinf->num_sph; i++) {
		if(intersect(ray, sphlist, &sp) && sp.t < sp0.t) {
			sp0 = sp;
		}
	}

	fb[idx] = shade(ray, sp0);
}

float4 shade(struct Ray ray, struct SurfPoint sp)
{
	return sp.obj->kd;
}

bool intersect(struct Ray ray,
		global const struct Sphere *sph,
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
