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
	float4 pos, norm;
	global const struct Sphere *obj;
};

#define EPSILON 1e-6

float4 shade(struct Ray ray, struct SurfPoint sp,
		global const struct Light *lights, int num_lights);
bool intersect(struct Ray ray, global const struct Sphere *sph, struct SurfPoint *sp);
float4 reflect(float4 v, float4 n);
float4 transform(float4 v, global const float *xform);
struct Ray transform_ray(global const struct Ray *ray, global const float *xform);


kernel void render(global float4 *fb,
		global const struct RendInfo *rinf,
		global const struct Sphere *sphlist,
		global const struct Light *lights,
		global const struct Ray *primrays,
		global const float *xform)
{
	int idx = get_global_id(0);

	struct Ray ray = transform_ray(primrays + idx, xform);

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
	float4 dcol = (float4)(0, 0, 0, 0);
	float4 scol = (float4)(0, 0, 0, 0);

	for(int i=0; i<num_lights; i++) {
		float4 ldir = normalize(lights[i].pos - sp.pos);
		float4 vdir = -normalize(ray.dir);
		float4 vref = reflect(vdir, sp.norm);

		float diff = fmax(dot(ldir, sp.norm), 0.0f);
		float spec = powr(fmax(dot(ldir, vref), 0.0f), sp.obj->spow);

		dcol += sp.obj->kd * diff * lights[i].color;
		scol += sp.obj->ks * spec * lights[i].color;
	}

	return dcol + scol;
}

bool intersect(struct Ray ray,
		global const struct Sphere *sph,
		struct SurfPoint *sp)
{
	float4 dir = ray.dir;
	float4 orig = ray.origin;
	float4 spos = sph->pos;

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

float4 reflect(float4 v, float4 n)
{
	return 2.0f * dot(v, n) * n - v;
}

float4 transform(float4 v, global const float *xform)
{
	float4 res;
	res.x = v.x * xform[0] + v.y * xform[4] + v.z * xform[8] + xform[12];
	res.y = v.x * xform[1] + v.y * xform[5] + v.z * xform[9] + xform[13];
	res.z = v.x * xform[2] + v.y * xform[6] + v.z * xform[10] + xform[14];
	res.w = 1.0;
	return res;
}

struct Ray transform_ray(global const struct Ray *ray, global const float *xform)
{
	struct Ray res;
	float rot[16];

	for(int i=0; i<16; i++) {
		rot[i] = xform[i];
	}
	rot[3] = rot[7] = rot[11] = rot[12] = rot[13] = rot[14] = 0.0f;
	rot[15] = 1.0f;

	res.origin = transform(ray->origin, xform);
	res.dir = transform(ray->dir, xform);
	return res;
}
