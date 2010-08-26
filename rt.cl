/* vim: set ft=opencl:ts=4:sw=4 */

struct RendInfo {
	float4 ambient;
	int xsz, ysz;
	int num_faces, num_lights;
	int max_iter;
	int kd_depth;
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
	struct Material mat;
};

struct Scene {
	float4 ambient;
	global const struct Face *faces;
	int num_faces;
	global const struct Light *lights;
	int num_lights;
	global const struct Material *matlib;
	global const struct KDNode *kdtree;
};

struct AABBox {
	float4 min, max;
};

struct KDNode {
	struct AABBox aabb;
	int face_idx[32];
	int num_faces;
	int left, right;
	int padding;
};

#define MIN_ENERGY	0.001
#define EPSILON		1e-5

float4 shade(struct Ray ray, struct Scene *scn, const struct SurfPoint *sp);
bool find_intersection(struct Ray ray, const struct Scene *scn, struct SurfPoint *sp);
bool intersect(struct Ray ray, global const struct Face *face, struct SurfPoint *sp);
bool intersect_aabb(struct Ray ray, struct AABBox aabb);

float4 reflect(float4 v, float4 n);
float4 transform(float4 v, global const float *xform);
void transform_ray(struct Ray *ray, global const float *xform, global const float *invtrans);
float4 calc_bary(float4 pt, global const struct Face *face, float4 norm);
float mean(float4 v);

kernel void render(global float4 *fb,
		global const struct RendInfo *rinf,
		global const struct Face *faces,
		global const struct Material *matlib,
		global const struct Light *lights,
		global const struct Ray *primrays,
		global const float *xform,
		global const float *invtrans,
		global const struct KDNode *kdtree)
{
	int idx = get_global_id(0);

	struct Scene scn;
	scn.ambient = rinf->ambient;
	scn.faces = faces;
	scn.num_faces = rinf->num_faces;
	scn.lights = lights;
	scn.num_lights = rinf->num_lights;
	scn.matlib = matlib;
	scn.kdtree = kdtree;

	struct Ray ray = primrays[idx];
	transform_ray(&ray, xform, invtrans);

	float4 pixel = (float4)(0, 0, 0, 0);
	float4 energy = (float4)(1.0, 1.0, 1.0, 0.0);
	int iter = 0;

	while(iter++ < rinf->max_iter && mean(energy) > MIN_ENERGY) {
		struct SurfPoint sp;
		if(find_intersection(ray, &scn, &sp)) {
			pixel += shade(ray, &scn, &sp) * energy;

			float4 refl_col = sp.mat.ks * sp.mat.kr;

			ray.origin = sp.pos;
			ray.dir = reflect(-ray.dir, sp.norm);

			energy *= refl_col;
		} else {
			break;
		}
	}

	fb[idx] = pixel;
}

float4 shade(struct Ray ray, struct Scene *scn, const struct SurfPoint *sp)
{
	float4 norm = sp->norm;
	bool entering = true;

	if(dot(ray.dir, norm) >= 0.0) {
		norm = -norm;
		entering = false;
	}

	float4 dcol = scn->ambient * sp->mat.kd;
	float4 scol = (float4)(0, 0, 0, 0);

	for(int i=0; i<scn->num_lights; i++) {
		float4 ldir = scn->lights[i].pos - sp->pos;

		struct Ray shadowray;
		shadowray.origin = sp->pos;
		shadowray.dir = ldir;

		if(!find_intersection(shadowray, scn, 0)) {
			ldir = normalize(ldir);
			float4 vdir = -normalize(ray.dir);
			float4 vref = reflect(vdir, norm);

			float diff = fmax(dot(ldir, norm), 0.0f);
			dcol += sp->mat.kd * scn->lights[i].color * diff;

			float spec = powr(fmax(dot(ldir, vref), 0.0f), sp->mat.spow);
			scol += sp->mat.ks * scn->lights[i].color * spec;
		}
	}

	return dcol + scol;
}

#define STACK_SIZE	64
bool find_intersection(struct Ray ray, const struct Scene *scn, struct SurfPoint *spres)
{
	struct SurfPoint sp0;
	sp0.t = 1.0;
	sp0.obj = 0;

	int idxstack[STACK_SIZE];
	int top = 0;			// points after the topmost element of the stack
	idxstack[top++] = 0;	// root at tree[0]

	while(top > 0) {
		int idx = idxstack[--top];	// remove this index from the stack and process it

		global const struct KDNode *node = scn->kdtree + idx;

		/*if(get_global_id(0) == 0) {
			for(int i=0; i<top+1; i++) {
				printf("   ");
			}
			printf("(%d) idx: %d (%p) num_faces: %d\n", top+1, idx, node, node->num_faces);
		}*/

		if(intersect_aabb(ray, node->aabb)) {
			if(node->left == -1) {
				// leaf node... check each face in turn and update the nearest intersection as needed
				for(int i=0; i<node->num_faces; i++) {
					struct SurfPoint spt;
					int fidx = node->face_idx[i];

					if(intersect(ray, scn->faces + fidx, &spt) && spt.t < sp0.t) {
						sp0 = spt;
					}
				}
			} else {
				// internal node... recurse to the children
				/*if(get_global_id(0) == 0) {
					printf("pushing %d's children %d and %d\n", idx, node->left, node->right);
				}*/
				idxstack[top++] = node->left;
				idxstack[top++] = node->right;
			}
		}
	}

	if(!sp0.obj) {
		return false;
	}

	if(spres) {
		*spres = sp0;
		spres->mat = scn->matlib[sp0.obj->matid];
	}
	return true;
}

/*bool find_intersection(struct Ray ray, const struct Scene *scn, struct SurfPoint *spres)
{
	struct SurfPoint sp, sp0;
	sp0.t = 1.0;
	sp0.obj = 0;

	for(int i=0; i<scn->num_faces; i++) {
		if(intersect(ray, scn->faces + i, &sp) && sp.t < sp0.t) {
			sp0 = sp;
		}
	}

	if(!sp0.obj) {
		return false;
	}

	if(spres) {
		*spres = sp0;
		spres->mat = scn->matlib[sp0.obj->matid];
	}
	return true;
}*/

bool intersect(struct Ray ray, global const struct Face *face, struct SurfPoint *sp)
{
	float4 origin = ray.origin;
	float4 dir = ray.dir;
	float4 norm = face->normal;

	float ndotdir = dot(dir, norm);

	if(fabs(ndotdir) <= EPSILON) {
		return false;
	}

	float4 pt = face->v[0].pos;
	float4 vec = pt - origin;

	float ndotvec = dot(norm, vec);
	float t = ndotvec / ndotdir;

	if(t < EPSILON || t > 1.0) {
		return false;
	}
	pt = origin + dir * t;


	float4 bc = calc_bary(pt, face, norm);
	float bc_sum = bc.x + bc.y + bc.z;

	if(bc_sum < 1.0 - EPSILON || bc_sum > 1.0 + EPSILON) {
		return false;
		bc *= 1.2;
	}

	sp->t = t;
	sp->pos = pt;
	sp->norm = normalize(face->v[0].normal * bc.x + face->v[1].normal * bc.y + face->v[2].normal * bc.z);
	sp->obj = face;
	sp->dbg = bc;
	return true;
}

bool intersect_aabb(struct Ray ray, struct AABBox aabb)
{
	if(ray.origin.x >= aabb.min.x && ray.origin.y >= aabb.min.y && ray.origin.z >= aabb.min.z &&
			ray.origin.x < aabb.max.x && ray.origin.y < aabb.max.y && ray.origin.z < aabb.max.z) {
		return true;
	}

	float4 bbox[2] = {
		aabb.min.x, aabb.min.y, aabb.min.z, 0,
		aabb.max.x, aabb.max.y, aabb.max.z, 0
	};

	int xsign = (int)(ray.dir.x < 0.0);
	float invdirx = 1.0 / ray.dir.x;
	float tmin = (bbox[xsign].x - ray.origin.x) * invdirx;
	float tmax = (bbox[1 - xsign].x - ray.origin.x) * invdirx;

	int ysign = (int)(ray.dir.y < 0.0);
	float invdiry = 1.0 / ray.dir.y;
	float tymin = (bbox[ysign].y - ray.origin.y) * invdiry;
	float tymax = (bbox[1 - ysign].y - ray.origin.y) * invdiry;

	if(tmin > tymax || tymin > tmax) {
		return false;
	}

	if(tymin > tmin) tmin = tymin;
	if(tymax < tmax) tmax = tymax;

	int zsign = (int)(ray.dir.z < 0.0);
	float invdirz = 1.0 / ray.dir.z;
	float tzmin = (bbox[zsign].z - ray.origin.z) * invdirz;
	float tzmax = (bbox[1 - zsign].z - ray.origin.z) * invdirz;

	if(tmin > tzmax || tzmin > tmax) {
		return false;
	}

	return tmin < 1.0 && tmax > 0.0;
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
	res.w = 0.0;
	return res;
}

void transform_ray(struct Ray *ray, global const float *xform, global const float *invtrans)
{
	ray->origin = transform(ray->origin, xform);
	ray->dir = transform(ray->dir, invtrans);
}

float4 calc_bary(float4 pt, global const struct Face *face, float4 norm)
{
	float4 bc = (float4)(0, 0, 0, 0);

	// calculate area of the whole triangle
	float4 v1 = face->v[1].pos - face->v[0].pos;
	float4 v2 = face->v[2].pos - face->v[0].pos;
	float4 xv1v2 = cross(v1, v2);

	float area = fabs(dot(xv1v2, norm)) * 0.5;
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

	float a0 = fabs(dot(x12, norm)) * 0.5;
	float a1 = fabs(dot(x20, norm)) * 0.5;
	float a2 = fabs(dot(x01, norm)) * 0.5;

	bc.x = a0 / area;
	bc.y = a1 / area;
	bc.z = a2 / area;
	return bc;
}

float mean(float4 v)
{
	return native_divide(v.x + v.y + v.z, 3.0);
}
