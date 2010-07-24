struct RendInfo {
	int xsz, ysz;
	int num_faces, num_lights;
	int max_iter;
};

struct Vertex {
	float4 pos;
	float4 normal;
	float2 tex;
};

struct Face {
	struct Vertex v[3];
	float4 normal;
	int matid;
};

struct Material {
	float4 kd, ks;
	float kr, kt;
	float spow;
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
	global const struct Face *obj;
	global const struct Material *mat;
};

#define EPSILON 1e-6

float4 shade(struct Ray ray, struct SurfPoint sp,
		global const struct Light *lights, int num_lights);
bool intersect(struct Ray ray, global const struct Face *face, struct SurfPoint *sp);
float4 reflect(float4 v, float4 n);
float4 transform(float4 v, global const float *xform);
struct Ray transform_ray(global const struct Ray *ray, global const float *xform);
float4 calc_bary(float4 pt, global const struct Face *face);


kernel void render(global float4 *fb,
		global const struct RendInfo *rinf,
		global const struct Face *faces,
		global const struct Material *matlib,
		global const struct Light *lights,
		global const struct Ray *primrays,
		global const float *xform)
{
	int idx = get_global_id(0);

	struct Ray ray = transform_ray(primrays + idx, xform);

	struct SurfPoint sp, sp0;
	sp0.t = FLT_MAX;
	sp0.obj = 0;

	for(int i=0; i<rinf->num_faces; i++) {
		if(intersect(ray, faces + i, &sp) && sp.t < sp0.t) {
			sp0 = sp;
		}
	}

	if(sp0.obj) {
		sp0.mat = matlib + sp0.obj->matid;
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
		float spec = powr(fmax(dot(ldir, vref), 0.0f), sp.mat->spow);

		dcol += sp.mat->kd * diff * lights[i].color;
		scol += sp.mat->ks * spec * lights[i].color;
	}

	return dcol + scol;
}

bool intersect(struct Ray ray,
		global const struct Face *face,
		struct SurfPoint *sp)
{
	float ndotdir = dot(face->normal, ray.dir);
	if(fabs(ndotdir) <= EPSILON) {
		return false;
	}

	float4 pt = face->v[0].pos;
	float4 vec = pt - ray.origin;

	float ndotvec = dot(face->normal, vec);
	float t = ndotvec / ndotdir;

	if(t < EPSILON || t > 1.0) {
		return false;
	}
	pt = ray.origin + ray.dir * t;

	float4 bc = calc_bary(pt, face);
	float bc_sum = bc.x + bc.y + bc.z;

	if(bc_sum < -EPSILON || bc_sum > 1.0) {
		return false;
	}

	sp->t = t;
	sp->pos = pt;
	sp->norm = face->normal;
	sp->obj = face;
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

float4 calc_bary(float4 pt, global const struct Face *face)
{
	float4 bc = {0, 0, 0, 0};

	float4 vi = face->v[1].pos - face->v[0].pos;
	float4 vj = face->v[2].pos - face->v[0].pos;
	float area = fabs(dot(cross(vi, vj), face->normal) / 2.0);
	if(area < EPSILON) {
		return bc;
	}

	float4 pv0 = face->v[0].pos - pt;
	float4 pv1 = face->v[1].pos - pt;
	float4 pv2 = face->v[2].pos - pt;

	// calculate the areas of each sub-triangle
	float a0 = fabs(dot(cross(pv1, pv2), face->normal) / 2.0);
	float a1 = fabs(dot(cross(pv2, pv0), face->normal) / 2.0);
	float a2 = fabs(dot(cross(pv0, pv1), face->normal) / 2.0);

	bc.x = a0 / area;
	bc.y = a1 / area;
	bc.z = a2 / area;
	return bc;
}
