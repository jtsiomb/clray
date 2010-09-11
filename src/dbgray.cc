#include <string.h>
#include <assert.h>
#include <limits.h>
#include "rt.h"
#include "ogl.h"
#include "vector.h"
#include "timer.h"

struct SurfPoint {
	float t;
	Vector3 pos, norm;
	const Face *face;
};

static void trace_ray(float *pixel, const Ray &ray, int iter, float energy = 1.0f);
static void shade(float *pixel, const Ray &ray, const SurfPoint &sp, int iter, float energy = 1.0f);
static bool find_intersection(const Ray &ray, const Scene *scn, const KDNode *kd, SurfPoint *spret);
static bool ray_aabb_test(const Ray &ray, const AABBox &aabb);
static bool ray_triangle_test(const Ray &ray, const Face *face, SurfPoint *sp);
static Vector3 calc_bary(const Vector3 &pt, const Face *face, const Vector3 &norm);
static void transform(float *res, const float *v, const float *xform);
static void transform_ray(Ray *ray, const float *xform, const float *invtrans_xform);

static int xsz, ysz;
static float *fb;
static unsigned int tex;
static Scene *scn;
static const Ray *prim_rays;
static int max_iter;

static RenderStats *rstat;
static int cur_ray_aabb_tests, cur_ray_triangle_tests;

bool init_dbg_renderer(int width, int height, Scene *scene, unsigned int texid)
{
	try {
		fb = new float[3 * width * height];
	}
	catch(...) {
		return false;
	}

	xsz = width;
	ysz = height;
	tex = texid;
	scn = scene;

	rstat = (RenderStats*)get_render_stats();

	return true;
}

void destroy_dbg_renderer()
{
	delete [] fb;
	delete [] prim_rays;
}

void dbg_set_primary_rays(const Ray *rays)
{
	prim_rays = rays;
}

void dbg_render(const float *xform, const float *invtrans_xform, int num_threads)
{
	unsigned long t0 = get_msec();

	max_iter = get_render_option_int(ROPT_ITER);

	// initialize render-stats
	memset(rstat, 0, sizeof *rstat);
	rstat->min_aabb_tests = rstat->min_triangle_tests = INT_MAX;
	rstat->max_aabb_tests = rstat->max_triangle_tests = 0;

	int offs = 0;
	for(int i=0; i<ysz; i++) {
		for(int j=0; j<xsz; j++) {
			Ray ray = prim_rays[offs];
			transform_ray(&ray, xform, invtrans_xform);

			cur_ray_aabb_tests = cur_ray_triangle_tests = 0;

			trace_ray(fb + offs * 3, ray, max_iter, 1.0);
			offs++;

			// update stats as needed
			if(cur_ray_aabb_tests < rstat->min_aabb_tests) {
				rstat->min_aabb_tests = cur_ray_aabb_tests;
			}
			if(cur_ray_aabb_tests > rstat->max_aabb_tests) {
				rstat->max_aabb_tests = cur_ray_aabb_tests;
			}
			if(cur_ray_triangle_tests < rstat->min_triangle_tests) {
				rstat->min_triangle_tests = cur_ray_triangle_tests;
			}
			if(cur_ray_triangle_tests > rstat->max_triangle_tests) {
				rstat->max_triangle_tests = cur_ray_triangle_tests;
			}
			rstat->prim_rays++;
			rstat->aabb_tests += cur_ray_aabb_tests;
			rstat->triangle_tests += cur_ray_triangle_tests;
		}
	}

	unsigned long t1 = get_msec();

	glPushAttrib(GL_TEXTURE_BIT);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, xsz, ysz, GL_RGB, GL_FLOAT, fb);
	glPopAttrib();
	glFinish();

	rstat->render_time = t1 - t0;
	rstat->tex_update_time = get_msec() - t1;

	rstat->rays_cast = rstat->prim_rays + rstat->refl_rays + rstat->shadow_rays;
	rstat->rays_per_sec = 1000 * rstat->rays_cast / rstat->render_time;
	rstat->avg_aabb_tests = (float)rstat->aabb_tests / (float)rstat->rays_cast;
	rstat->avg_triangle_tests = (float)rstat->triangle_tests / (float)rstat->rays_cast;
}

static void trace_ray(float *pixel, const Ray &ray, int iter, float energy)
{
	SurfPoint sp;

	if(find_intersection(ray, scn, scn->kdtree, &sp)) {
		shade(pixel, ray, sp, iter, energy);
	} else {
		pixel[0] = pixel[1] = pixel[2] = 0.05f;
	}
}

#define MAX(a, b)	((a) > (b) ? (a) : (b))

static void shade(float *pixel, const Ray &ray, const SurfPoint &sp, int iter, float energy)
{
	const Material *mat = scn->get_materials() + sp.face->matid;

	bool cast_shadows = get_render_option_bool(ROPT_SHAD);
	Vector3 raydir(ray.dir);
	Vector3 norm = sp.norm;

	if(dot(raydir, norm) >= 0.0) {
		norm = -norm;
	}

	float dcol[3] = {0, 0, 0};
	float scol[3] = {0, 0, 0};

	for(int i=0; i<scn->get_num_lights(); i++) {
		Vector3 lpos(scn->lights[i].pos);
		Vector3 ldir = lpos - sp.pos;

		Ray shadowray;
		shadowray.origin[0] = sp.pos.x;
		shadowray.origin[1] = sp.pos.y;
		shadowray.origin[2] = sp.pos.z;
		shadowray.dir[0] = ldir.x;
		shadowray.dir[1] = ldir.y;
		shadowray.dir[2] = ldir.z;

		if(!cast_shadows || !find_intersection(shadowray, scn, scn->kdtree, 0)) {
			rstat->brdf_evals++;

			ldir.normalize();

			Vector3 vdir = -raydir / RAY_MAG;
			Vector3 vref = reflect(vdir, norm);

			float ndotl = dot(ldir, norm);
			float diff = MAX(ndotl, 0.0f);

			dcol[0] += mat->kd[0] * diff;
			dcol[1] += mat->kd[1] * diff;
			dcol[2] += mat->kd[2] * diff;

			float ldotvr = dot(ldir, vref);
			float spec = pow(MAX(ldotvr, 0.0f), mat->spow);

			scol[0] += mat->ks[0] * spec;
			scol[1] += mat->ks[1] * spec;
			scol[2] += mat->ks[2] * spec;
		}

		if(cast_shadows) {
			rstat->shadow_rays++;
		}
	}

	float refl_color[3];
	refl_color[0] = mat->ks[0] * mat->kr;
	refl_color[1] = mat->ks[1] * mat->kr;
	refl_color[2] = mat->ks[2] * mat->kr;

	energy *= (refl_color[0] + refl_color[1] + refl_color[2]) / 3.0;
	if(iter >= 0 && energy > MIN_ENERGY) {
		Vector3 rdir = reflect(-raydir, norm);

		Ray refl;
		refl.origin[0] = sp.pos.x;
		refl.origin[1] = sp.pos.y;
		refl.origin[2] = sp.pos.z;
		refl.dir[0] = rdir.x;
		refl.dir[1] = rdir.y;
		refl.dir[2] = rdir.z;

		float rcol[3];
		trace_ray(rcol, refl, iter - 1, energy);
		scol[0] += rcol[0] * mat->ks[0] * mat->kr;
		scol[1] += rcol[1] * mat->ks[1] * mat->kr;
		scol[2] += rcol[2] * mat->ks[2] * mat->kr;

		rstat->refl_rays++;
	}

	pixel[0] = dcol[0] + scol[0];
	pixel[1] = dcol[1] + scol[1];
	pixel[2] = dcol[2] + scol[2];
}

static bool find_intersection(const Ray &ray, const Scene *scn, const KDNode *kd, SurfPoint *spret)
{
	if(!ray_aabb_test(ray, kd->aabb)) {
		return false;
	}

	SurfPoint sp, sptmp;
	if(!spret) {
		spret = &sptmp;
	}

	spret->t = RAY_MAG;
	spret->face = 0;

	if(kd->left) {
		assert(kd->right);

		bool found = find_intersection(ray, scn, kd->left, spret);
		if(find_intersection(ray, scn, kd->right, &sp)) {
			if(!found || sp.t < spret->t) {
				*spret = sp;
			}
			found = true;
		}
		return found;
	}

	const Face *faces = scn->get_face_buffer();

	for(size_t i=0; i<kd->face_idx.size(); i++) {
		if(ray_triangle_test(ray, faces + kd->face_idx[i], &sp) && sp.t < spret->t) {
			*spret = sp;
		}
	}
	return spret->face != 0;
}

static bool ray_aabb_test(const Ray &ray, const AABBox &aabb)
{
	cur_ray_aabb_tests++;

	if(ray.origin[0] >= aabb.min[0] && ray.origin[1] >= aabb.min[1] && ray.origin[2] >= aabb.min[2] &&
			ray.origin[0] < aabb.max[0] && ray.origin[1] < aabb.max[1] && ray.origin[2] < aabb.max[2]) {
		return true;
	}

	float bbox[][3] = {
		{aabb.min[0], aabb.min[1], aabb.min[2]},
		{aabb.max[0], aabb.max[1], aabb.max[2]}
	};

	int xsign = (int)(ray.dir[0] < 0.0);
	float invdirx = 1.0 / ray.dir[0];
	float tmin = (bbox[xsign][0] - ray.origin[0]) * invdirx;
	float tmax = (bbox[1 - xsign][0] - ray.origin[0]) * invdirx;

	int ysign = (int)(ray.dir[1] < 0.0);
	float invdiry = 1.0 / ray.dir[1];
	float tymin = (bbox[ysign][1] - ray.origin[1]) * invdiry;
	float tymax = (bbox[1 - ysign][1] - ray.origin[1]) * invdiry;

	if(tmin > tymax || tymin > tmax) {
		return false;
	}

	if(tymin > tmin) tmin = tymin;
	if(tymax < tmax) tmax = tymax;

	int zsign = (int)(ray.dir[2] < 0.0);
	float invdirz = 1.0 / ray.dir[2];
	float tzmin = (bbox[zsign][2] - ray.origin[2]) * invdirz;
	float tzmax = (bbox[1 - zsign][2] - ray.origin[2]) * invdirz;

	if(tmin > tzmax || tzmin > tmax) {
		return false;
	}

	return tmin < 1.0 && tmax > 0.0;

}

static bool ray_triangle_test(const Ray &ray, const Face *face, SurfPoint *sp)
{
	cur_ray_triangle_tests++;

	Vector3 origin = ray.origin;
	Vector3 dir = ray.dir;
	Vector3 norm = face->normal;

	float ndotdir = dot(dir, norm);

	if(fabs(ndotdir) <= EPSILON) {
		return false;
	}

	Vector3 pt = face->v[0].pos;
	Vector3 vec = pt - origin;

	float ndotvec = dot(norm, vec);
	float t = ndotvec / ndotdir;

	if(t < EPSILON || t > 1.0) {
		return false;
	}
	pt = origin + dir * t;


	Vector3 bc = calc_bary(pt, face, norm);
	float bc_sum = bc.x + bc.y + bc.z;

	if(bc_sum < 1.0 - EPSILON || bc_sum > 1.0 + EPSILON) {
		return false;
	}

	Vector3 n0(face->v[0].normal);
	Vector3 n1(face->v[1].normal);
	Vector3 n2(face->v[2].normal);

	sp->t = t;
	sp->pos = pt;
	sp->norm = n0 * bc.x + n1 * bc.y + n2 * bc.z;
	sp->norm.normalize();
	sp->face = face;
	return true;
}

static Vector3 calc_bary(const Vector3 &pt, const Face *face, const Vector3 &norm)
{
	Vector3 bc(0.0f, 0.0f, 0.0f);

	Vector3 v1 = Vector3(face->v[1].pos) - Vector3(face->v[0].pos);
	Vector3 v2 = Vector3(face->v[2].pos) - Vector3(face->v[0].pos);
	Vector3 xv1v2 = cross(v1, v2);

	float area = fabs(dot(xv1v2, norm)) * 0.5;
	if(area < EPSILON) {
		return bc;
	}

	Vector3 pv0 = face->v[0].pos - pt;
	Vector3 pv1 = face->v[1].pos - pt;
	Vector3 pv2 = face->v[2].pos - pt;

	// calculate the area of each sub-triangle
	Vector3 x12 = cross(pv1, pv2);
	Vector3 x20 = cross(pv2, pv0);
	Vector3 x01 = cross(pv0, pv1);

	float a0 = fabs(dot(x12, norm)) * 0.5;
	float a1 = fabs(dot(x20, norm)) * 0.5;
	float a2 = fabs(dot(x01, norm)) * 0.5;

	bc.x = a0 / area;
	bc.y = a1 / area;
	bc.z = a2 / area;
	return bc;

}

static void transform(float *res, const float *v, const float *xform)
{
	float tmp[3];
	tmp[0] = v[0] * xform[0] + v[1] * xform[4] + v[2] * xform[8] + xform[12];
	tmp[1] = v[0] * xform[1] + v[1] * xform[5] + v[2] * xform[9] + xform[13];
	tmp[2] = v[0] * xform[2] + v[1] * xform[6] + v[2] * xform[10] + xform[14];
	memcpy(res, tmp, sizeof tmp);
}

static void transform_ray(Ray *ray, const float *xform, const float *invtrans_xform)
{
	transform(ray->origin, ray->origin, xform);
	transform(ray->dir, ray->dir, invtrans_xform);
}
