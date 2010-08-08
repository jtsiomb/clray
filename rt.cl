/* vim: set ft=opencl:ts=4:sw=4 */

struct RendInfo {
	int xsz, ysz;
	int num_faces, num_lights;
	int max_iter;
	int dbg;
};

struct Vertex {
	float4 pos;
	float4 normal;
	float4 tex;
	float4 padding;
};

struct Face {
	struct Vertex v[3];
	float4 normal;
	int matid;
	int padding[3];
};

struct Material {
	float4 kd, ks;
	float kr, kt;
	float spow;
	float padding;
};

struct Light {
	float4 pos, color;
};

struct Ray {
	float4 origin, dir;
};

struct SurfPoint {
	float t;
	float4 pos, norm, dbg;
	global const struct Face *obj;
	global const struct Material *mat;
};

#define EPSILON 1e-6

float4 shade(struct Ray ray, struct SurfPoint sp,
		global const struct Light *lights, int num_lights);
bool intersect(struct Ray ray, global const struct Face *face, struct SurfPoint *sp);
float4 reflect(float4 v, float4 n);
float4 transform(float4 v, global const float *xform);
struct Ray transform_ray(global const struct Ray *ray, global const float *xform, global const float *invtrans);
float4 calc_bary(float4 pt, global const struct Face *face, float4 norm);

kernel void render(global float4 *fb,
		global const struct RendInfo *rinf,
		global const struct Face *faces,
		global const struct Material *matlib,
		global const struct Light *lights,
		global const struct Ray *primrays,
		global const float *xform,
		global const float *invtrans,
		global struct Face *outfaces)
{
	int idx = get_global_id(0);
	
	if(idx == 0) {
		for(int i=0; i<rinf->num_faces; i++) {
			outfaces[i] = faces[i];
		}
	}

	struct Ray ray = transform_ray(primrays + idx, xform, invtrans);

	struct SurfPoint sp, sp0;
	sp0.t = FLT_MAX;
	sp0.obj = 0;
	
	int max_faces = min(rinf->num_faces, rinf->dbg);

	for(int i=0; i<max_faces; i++) {
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
	float4 norm = sp.norm;
	bool entering = true;

	if(dot(ray.dir, norm) >= 0.0) {
		norm = -norm;
		entering = false;
	}

	float4 dcol = (float4)(0.07, 0.07, 0.07, 0);
	float4 scol = (float4)(0, 0, 0, 0);

	for(int i=0; i<num_lights; i++) {
		float4 ldir = normalize(lights[i].pos - sp.pos);
		float4 vdir = -normalize(ray.dir);
		float4 vref = reflect(vdir, norm);

		float diff = fmax(dot(ldir, norm), 0.0f);
		float spec = powr(fmax(dot(ldir, vref), 0.0f), sp.mat->spow);

		dcol += sp.mat->kd * diff * lights[i].color;
		//scol += sp.mat->ks * spec * lights[i].color;
	}

	return dcol + scol;
}

float dot3(float4 a, float4 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}


bool intersect(struct Ray ray,
		global const struct Face *face,
		struct SurfPoint *sp)
{
	float4 origin = ray.origin;
	float4 dir = ray.dir;
	float4 norm = face->normal;

	float ndotdir = dot3(dir, norm);

	if(fabs(ndotdir) <= EPSILON) {
		return false;
	}

	float4 pt = face->v[0].pos;
	float4 vec = pt - origin;

	float ndotvec = dot3(norm, vec);
	float t = ndotvec / ndotdir;

	if(t < EPSILON || t > 1.0) {
		return false;
	}
	pt = origin + dir * t;

	if(pt.w < 0.0) return false;


	float4 bc = calc_bary(pt, face, norm);
	float bc_sum = bc.x + bc.y + bc.z;

	if(bc_sum < 0.0 || bc_sum > 1.0 + EPSILON) {
		return false;
		bc *= 1.2;
	}

	sp->t = t;
	sp->pos = pt;
	sp->norm = norm;
	sp->obj = face;
	sp->dbg = bc;
	return true;
}

float4 reflect(float4 v, float4 n)
{
	float4 res = 2.0f * dot(v, n) * n - v;
	return res;
}

float4 transform(float4 v, global const float *xform)
{
	float4 res;
	res.x = v.x * xform[0] + v.y * xform[4] + v.z * xform[8] + xform[12];
	res.y = v.x * xform[1] + v.y * xform[5] + v.z * xform[9] + xform[13];
	res.z = v.x * xform[2] + v.y * xform[6] + v.z * xform[10] + xform[14];
	res.w = 0.0;
	return res;
}

struct Ray transform_ray(global const struct Ray *ray, global const float *xform, global const float *invtrans)
{
	struct Ray res;
	res.origin = transform(ray->origin, xform);
	res.dir = transform(ray->dir, invtrans);
	return res;
}

float4 calc_bary(float4 pt, global const struct Face *face, float4 norm)
{
	float4 bc = (float4)(0, 0, 0, 0);

	// calculate area of the whole triangle
	float4 v1 = face->v[1].pos - face->v[0].pos;
	float4 v2 = face->v[2].pos - face->v[0].pos;
	float4 xv1v2 = cross(v1, v2);

	float area = fabs(dot3(xv1v2, norm)) * 0.5;
	if(area < EPSILON) {
		return bc;
	}

	float4 pv0 = face->v[0].pos - pt;
	float4 pv1 = face->v[1].pos - pt;
	float4 pv2 = face->v[2].pos - pt;

	// calculate the area of each sub-triangle
	float4 x12 = cross(pv1, pv2);
	float4 x20 = cross(pv2, pv0);
	float4 x01 = cross(pv0, pv1);

	float a0 = fabs(dot3(x12, norm)) * 0.5;
	float a1 = fabs(dot3(x20, norm)) * 0.5;
	float a2 = fabs(dot3(x01, norm)) * 0.5;

	bc.x = a0 / area;
	bc.y = a1 / area;
	bc.z = a2 / area;
	return bc;
}
