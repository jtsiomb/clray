struct RendInfo {
	int xsz, ysz;
	int num_sph, num_lights;
	int max_iter;
};

struct Sphere {
	float4 pos;
	float4 kd, ks;
	float radius;
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

float4 shade(struct Ray ray, struct SurfPoint sp,
		global const struct Light *lights, int num_lights);
bool intersect(struct Ray ray, global const struct Sphere *sph, struct SurfPoint *sp);
float3 reflect(float3 v, float3 n);


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
	sp0.obj = 0;

	for(int i=0; i<rinf->num_sph; i++) {
		if(intersect(ray, sphlist + i, &sp) && sp.t < sp0.t) {
			sp0 = sp;
		}
	}

	if(sp0.obj) {
		fb[idx] = shade(ray, sp0, lights, rinf->num_lights);
	} else {
		fb[idx] = (float4)(0, 0, 0, 0);
	}
}

float4 shade(struct Ray ray, struct SurfPoint sp,
		global const struct Light *lights, int num_lights)
{
	float3 dcol = (float3)(0, 0, 0);
	float3 scol = (float3)(0, 0, 0);

	for(int i=0; i<num_lights; i++) {
		float3 ldir = normalize(lights[i].pos.xyz - sp.pos);
		float3 vdir = -normalize(ray.dir.xyz);
		float3 vref = reflect(vdir, sp.norm);

		float diff = fmax(dot(ldir, sp.norm), 0.0f);
		float spec = powr(fmax(dot(ldir, vref), 0.0f), sp.obj->spow);

		dcol += sp.obj->kd.xyz * diff * lights[i].color.xyz;
		scol += sp.obj->ks.xyz * spec * lights[i].color.xyz;
	}

	return (float4)(dcol + scol, 1.0f);
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
	sp->obj = sph;
	return true;
}

float3 reflect(float3 v, float3 n)
{
	return 2.0f * dot(v, n) * n - v;
}
