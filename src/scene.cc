#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <assert.h>
#include <map>
#include "scene.h"
#include "ogl.h"
#include "vector.h"

#define CHECK_AABB(aabb)	\
	assert(aabb.max[0] >= aabb.min[0] && aabb.max[1] >= aabb.min[1] && aabb.max[2] >= aabb.min[2])


#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) > (b) ? (a) : (b))


static int flatten_kdtree(const KDNode *node, KDNodeGPU *kdbuf, int *count);
static void draw_kdtree(const KDNode *node, int level = 0);
static bool build_kdtree(KDNode *kd, const Face *faces, int level = 0);
static float eval_cost(const Face *faces, const int *face_idx, int num_faces, const AABBox &aabb, int axis);
static void free_kdtree(KDNode *node);
static void print_item_counts(const KDNode *node, int level);
static int clip_face(const Face &inface, float splitpos, int axis, int sign, Face *faces);
static float calc_sq_area(const Vector3 &a, const Vector3 &b, const Vector3 &c);


static int accel_param[NUM_ACCEL_PARAMS] = {
	64,	// max tree depth
	MAX_NODE_FACES,	// max items per node (0 means ignore limit)
	5,	// estimated traversal cost
	15	// estimated interseciton cost
};


void set_accel_param(int p, int v)
{
	assert(p >= 0 && p < NUM_ACCEL_PARAMS);
	accel_param[p] = v;
}


float AABBox::calc_surface_area() const
{
	float area1 = (max[0] - min[0]) * (max[1] - min[1]);
	float area2 = (max[3] - min[3]) * (max[1] - min[1]);
	float area3 = (max[0] - min[0]) * (max[3] - min[3]);

	return 2.0f * (area1 + area2 + area3);
}

KDNode::KDNode()
{
	left = right = 0;
	cost = 0.0;
}


Scene::Scene()
{
	facebuf = 0;
	num_faces = -1;
	kdtree = 0;
	kdbuf = 0;
}

Scene::~Scene()
{
	delete [] facebuf;
	delete [] kdbuf;
	free_kdtree(kdtree);
}

bool Scene::add_mesh(Mesh *m)
{
	// make sure triangles have material ids
	for(size_t i=0; i<m->faces.size(); i++) {
		m->faces[i].matid = m->matid;
	}

	try {
		meshes.push_back(m);
	}
	catch(...) {
		return false;
	}

	// invalidate facebuffer and count
	delete [] facebuf;
	facebuf = 0;
	num_faces = -1;

	return true;
}

bool Scene::add_light(const Light &lt)
{
	try {
		lights.push_back(lt);
	}
	catch(...) {
		return false;
	}
	return true;
}

int Scene::get_num_meshes() const
{
	return (int)meshes.size();
}

int Scene::get_num_lights() const
{
	return (int)lights.size();
}

int Scene::get_num_faces() const
{
	if(num_faces >= 0) {
		return num_faces;
	}

	num_faces = 0;
	for(size_t i=0; i<meshes.size(); i++) {
		num_faces += meshes[i]->faces.size();
	}
	return num_faces;
}

int Scene::get_num_materials() const
{
	return (int)matlib.size();
}

int Scene::get_num_kdnodes() const
{
	return kdtree_nodes(kdtree);
}

Mesh **Scene::get_meshes()
{
	if(meshes.empty()) {
		return 0;
	}
	return &meshes[0];
}

const Mesh * const *Scene::get_meshes() const
{
	if(meshes.empty()) {
		return 0;
	}
	return &meshes[0];
}

Light *Scene::get_lights()
{
	if(lights.empty()) {
		return 0;
	}
	return &lights[0];
}

const Light *Scene::get_lights() const
{
	if(lights.empty()) {
		return 0;
	}
	return &lights[0];
}

Material *Scene::get_materials()
{
	if(matlib.empty()) {
		return 0;
	}
	return &matlib[0];
}

const Material *Scene::get_materials() const
{
	if(matlib.empty()) {
		return 0;
	}
	return &matlib[0];
}

const Face *Scene::get_face_buffer() const
{
	if(facebuf) {
		return facebuf;
	}

	int num_meshes = get_num_meshes();

	printf("constructing face buffer with %d faces (out of %d meshes)\n", get_num_faces(), num_meshes);
	facebuf = new Face[num_faces];
	Face *fptr = facebuf;

	for(int i=0; i<num_meshes; i++) {
		for(size_t j=0; j<meshes[i]->faces.size(); j++) {
			*fptr++ = meshes[i]->faces[j];
		}
	}
	return facebuf;
}

const KDNodeGPU *Scene::get_kdtree_buffer() const
{
	if(kdbuf) {
		return kdbuf;
	}

	if(!kdtree) {
		((Scene*)this)->build_kdtree();
	}

	int num_nodes = get_num_kdnodes();
	kdbuf = new KDNodeGPU[num_nodes];

	int count = 0;

	// first arrange the kdnodes into an array (flatten)
	flatten_kdtree(kdtree, kdbuf, &count);

	return kdbuf;
}

static int flatten_kdtree(const KDNode *node, KDNodeGPU *kdbuf, int *count)
{
	const size_t max_node_items = sizeof kdbuf[0].face_idx / sizeof kdbuf[0].face_idx[0];
	int idx = (*count)++;

	// copy the node
	kdbuf[idx].aabb = node->aabb;
	kdbuf[idx].num_faces = 0;

	for(size_t i=0; i<node->face_idx.size(); i++) {
		if(i >= max_node_items) {
			fprintf(stderr, "WARNING too many faces per leaf node!\n");
			break;
		}
		kdbuf[idx].face_idx[i] = node->face_idx[i];
		kdbuf[idx].num_faces++;
	}

	// recurse to the left/right (if we're not in a leaf node)
	if(node->left) {
		assert(node->right);

		kdbuf[idx].left = flatten_kdtree(node->left, kdbuf, count);
		kdbuf[idx].right = flatten_kdtree(node->right, kdbuf, count);
	} else {
		kdbuf[idx].left = kdbuf[idx].right = -1;
	}

	return idx;
}

void Scene::draw_kdtree() const
{
	glPushAttrib(GL_ENABLE_BIT);
	glDisable(GL_LIGHTING);
	glDepthMask(0);

	glBegin(GL_LINES);
	::draw_kdtree(kdtree, 0);
	glEnd();

	glDepthMask(1);
	glPopAttrib();
}

static float palette[][3] = {
	{0, 1, 0},
	{1, 0, 0},
	{0, 0, 1},
	{1, 1, 0},
	{0, 0, 1},
	{1, 0, 1}
};
static int pal_size = sizeof palette / sizeof *palette;

static void draw_kdtree(const KDNode *node, int level)
{
	if(!node) return;

	draw_kdtree(node->left, level + 1);
	draw_kdtree(node->right, level + 1);

	glColor3fv(palette[level % pal_size]);

	glVertex3fv(node->aabb.min);
	glVertex3f(node->aabb.max[0], node->aabb.min[1], node->aabb.min[2]);
	glVertex3f(node->aabb.max[0], node->aabb.min[1], node->aabb.min[2]);
	glVertex3f(node->aabb.max[0], node->aabb.max[1], node->aabb.min[2]);
	glVertex3f(node->aabb.max[0], node->aabb.max[1], node->aabb.min[2]);
	glVertex3f(node->aabb.min[0], node->aabb.max[1], node->aabb.min[2]);
	glVertex3f(node->aabb.min[0], node->aabb.max[1], node->aabb.min[2]);
	glVertex3fv(node->aabb.min);

	glVertex3f(node->aabb.min[0], node->aabb.min[1], node->aabb.max[2]);
	glVertex3f(node->aabb.max[0], node->aabb.min[1], node->aabb.max[2]);
	glVertex3f(node->aabb.max[0], node->aabb.min[1], node->aabb.max[2]);
	glVertex3fv(node->aabb.max);
	glVertex3fv(node->aabb.max);
	glVertex3f(node->aabb.min[0], node->aabb.max[1], node->aabb.max[2]);
	glVertex3f(node->aabb.min[0], node->aabb.max[1], node->aabb.max[2]);
	glVertex3f(node->aabb.min[0], node->aabb.min[1], node->aabb.max[2]);

	glVertex3fv(node->aabb.min);
	glVertex3f(node->aabb.min[0], node->aabb.min[1], node->aabb.max[2]);
	glVertex3f(node->aabb.max[0], node->aabb.min[1], node->aabb.min[2]);
	glVertex3f(node->aabb.max[0], node->aabb.min[1], node->aabb.max[2]);
	glVertex3f(node->aabb.max[0], node->aabb.max[1], node->aabb.min[2]);
	glVertex3fv(node->aabb.max);
	glVertex3f(node->aabb.min[0], node->aabb.max[1], node->aabb.min[2]);
	glVertex3f(node->aabb.min[0], node->aabb.max[1], node->aabb.max[2]);
}

bool Scene::build_kdtree()
{
	assert(kdtree == 0);

	const Face *faces = get_face_buffer();
	int num_faces = get_num_faces();

	printf("Constructing kd-tree out of %d faces ...\n", num_faces);

	int icost = accel_param[ACCEL_PARAM_COST_INTERSECT];
	int tcost = accel_param[ACCEL_PARAM_COST_TRAVERSE];
	printf("  max items per leaf: %d\n", accel_param[ACCEL_PARAM_MAX_NODE_ITEMS]);
	printf("  SAH parameters - tcost: %d - icost: %d\n", tcost, icost);

	free_kdtree(kdtree);
	kdtree = new KDNode;

	/* Start the construction of the kdtree by adding all faces of the scene
	 * to the new root node. At the same time calculate the root's AABB.
	 */
	kdtree->aabb.min[0] = kdtree->aabb.min[1] = kdtree->aabb.min[2] = FLT_MAX;
	kdtree->aabb.max[0] = kdtree->aabb.max[1] = kdtree->aabb.max[2] = -FLT_MAX;

	for(int i=0; i<num_faces; i++) {
		const Face *face = faces + i;

		// for each vertex of the face ...
		for(int j=0; j<3; j++) {
			const float *pos = face->v[j].pos;

			// for each element (xyz) of the position vector ...
			for(int k=0; k<3; k++) {
				if(pos[k] < kdtree->aabb.min[k]) {
					kdtree->aabb.min[k] = pos[k];
				}
				if(pos[k] > kdtree->aabb.max[k]) {
					kdtree->aabb.max[k] = pos[k];
				}
			}
		}

		kdtree->face_idx.push_back(i);	// add the face
	}

	CHECK_AABB(kdtree->aabb);

	// calculate the heuristic for the root
	kdtree->cost = eval_cost(faces, &kdtree->face_idx[0], kdtree->face_idx.size(), kdtree->aabb, 0);

	// now proceed splitting the root recursively
	if(!::build_kdtree(kdtree, faces)) {
		fprintf(stderr, "failed to build kdtree\n");
		return false;
	}

	printf("  tree depth: %d\n", kdtree_depth(kdtree));
	print_item_counts(kdtree, 0);
	return true;
}

struct Split {
	int axis;
	float pos;
	float sum_cost;
	float cost_left, cost_right;
};

static void find_best_split(const KDNode *node, int axis, const Face *faces, Split *split)
{
	Split best_split;
	best_split.sum_cost = FLT_MAX;

	for(size_t i=0; i<node->face_idx.size(); i++) {
		const Face *face = faces + node->face_idx[i];

		float splitpt[2];
		splitpt[0] = MIN(face->v[0].pos[axis], MIN(face->v[1].pos[axis], face->v[2].pos[axis]));
		splitpt[1] = MAX(face->v[0].pos[axis], MAX(face->v[1].pos[axis], face->v[2].pos[axis]));

		for(int j=0; j<2; j++) {
			if(splitpt[j] <= node->aabb.min[axis] || splitpt[j] >= node->aabb.max[axis]) {
				continue;
			}

			AABBox aabb_left, aabb_right;
			aabb_left = aabb_right = node->aabb;
			aabb_left.max[axis] = splitpt[j];
			aabb_right.min[axis] = splitpt[j];

			float left_cost = eval_cost(faces, &node->face_idx[0], node->face_idx.size(), aabb_left, axis);
			float right_cost = eval_cost(faces, &node->face_idx[0], node->face_idx.size(), aabb_right, axis);
			float sum_cost = left_cost + right_cost - accel_param[ACCEL_PARAM_COST_TRAVERSE]; // tcost is added twice

			if(sum_cost < best_split.sum_cost) {
				best_split.cost_left = left_cost;
				best_split.cost_right = right_cost;
				best_split.sum_cost = sum_cost;
				best_split.pos = splitpt[j];
			}
		}
	}

	assert(split);
	*split = best_split;
	split->axis = axis;
}

static bool build_kdtree(KDNode *kd, const Face *faces, int level)
{
	int opt_max_depth = accel_param[ACCEL_PARAM_MAX_TREE_DEPTH];
	int opt_max_items = accel_param[ACCEL_PARAM_MAX_NODE_ITEMS];

	if(kd->face_idx.empty() || level >= opt_max_depth) {
		return true;
	}

	Split best_split;
	best_split.axis = -1;
	best_split.sum_cost = FLT_MAX;

	for(int i=0; i<3; i++) {
		Split split;
		find_best_split(kd, i, faces, &split);

		if(split.sum_cost < best_split.sum_cost) {
			best_split = split;
		}
	}

	if(best_split.axis == -1) {
		return true;	// can't split any more, only 0-area splits available
	}

	//printf("current cost: %f,   best_cost: %f\n", kd->cost, best_sum_cost);
	if(best_split.sum_cost > kd->cost && (opt_max_items == 0 || (int)kd->face_idx.size() <= opt_max_items)) {
		return true;	// stop splitting if it doesn't reduce the cost
	}

	kd->axis = best_split.axis;

	// create the two children
	KDNode *kdleft, *kdright;
	kdleft = new KDNode;
	kdright = new KDNode;

	kdleft->aabb = kdright->aabb = kd->aabb;

	kdleft->aabb.max[kd->axis] = best_split.pos;
	kdright->aabb.min[kd->axis] = best_split.pos;

	kdleft->cost = best_split.cost_left;
	kdright->cost = best_split.cost_right;

	// TODO would it be much better if we actually split faces that straddle the splitting plane?
	for(size_t i=0; i<kd->face_idx.size(); i++) {
		int fidx = kd->face_idx[i];
		const Face *face = faces + fidx;

		if(face->v[0].pos[kd->axis] < best_split.pos ||
				face->v[1].pos[kd->axis] < best_split.pos ||
				face->v[2].pos[kd->axis] < best_split.pos) {
			kdleft->face_idx.push_back(fidx);
		}
		if(face->v[0].pos[kd->axis] >= best_split.pos ||
				face->v[1].pos[kd->axis] >= best_split.pos ||
				face->v[2].pos[kd->axis] >= best_split.pos) {
			kdright->face_idx.push_back(fidx);
		}
	}
	kd->face_idx.clear();	// only leaves have faces

	kd->left = kdleft;
	kd->right = kdright;

	return build_kdtree(kd->left, faces, level + 1) && build_kdtree(kd->right, faces, level + 1);
}

static float eval_cost(const Face *faces, const int *face_idx, int num_faces, const AABBox &aabb, int axis)
{
	int num_inside = 0;
	int tcost = accel_param[ACCEL_PARAM_COST_TRAVERSE];
	int icost = accel_param[ACCEL_PARAM_COST_INTERSECT];

	for(int i=0; i<num_faces; i++) {
		const Face *face = faces + face_idx[i];

		for(int j=0; j<3; j++) {
			if(face->v[j].pos[axis] >= aabb.min[axis] && face->v[j].pos[axis] < aabb.max[axis]) {
				num_inside++;
				break;
			}
		}
	}

	float dx = aabb.max[0] - aabb.min[0];
	float dy = aabb.max[1] - aabb.min[1];
	float dz = aabb.max[2] - aabb.min[2];

	if(dx < 0.0) {
		fprintf(stderr, "FOO DX = %f\n", dx);
		abort();
	}
	if(dy < 0.0) {
		fprintf(stderr, "FOO DX = %f\n", dy);
		abort();
	}
	if(dz < 0.0) {
		fprintf(stderr, "FOO DX = %f\n", dz);
		abort();
	}

	if(dx < 1e-6 || dy < 1e-6 || dz < 1e-6) {
		return FLT_MAX;	// heavily penalize 0-area voxels
	}

	float sarea = 2.0 * (dx + dy + dz);//aabb.calc_surface_area();
	return tcost + sarea * num_inside * icost;
}

static void free_kdtree(KDNode *node)
{
	if(node) {
		free_kdtree(node->left);
		free_kdtree(node->right);
		delete node;
	}
}

int kdtree_depth(const KDNode *node)
{
	if(!node) return 0;

	int left = kdtree_depth(node->left);
	int right = kdtree_depth(node->right);
	return (left > right ? left : right) + 1;
}

int kdtree_nodes(const KDNode *node)
{
	if(!node) return 0;
	return kdtree_nodes(node->left) + kdtree_nodes(node->right) + 1;
}

static void print_item_counts(const KDNode *node, int level)
{
	if(!node) return;

	for(int i=0; i<level; i++) {
		fputs("   ", stdout);
	}
	printf("- %d (cost: %f)\n", (int)node->face_idx.size(), node->cost);

	print_item_counts(node->left, level + 1);
	print_item_counts(node->right, level + 1);
}

#define SGN(x)		((x) >= 0 ? 1 : -1)
#define INSIDE(x)	(SGN((x) - (splitpos)) == sign)
#define OUTSIDE(x)	(!INSIDE(x))

#define LERPV3(res, a, b, t) \
	do { \
		(res)[0] = (a)[0] + ((b)[0] - (a)[0]) * (t); \
		(res)[1] = (a)[1] + ((b)[1] - (a)[1]) * (t); \
		(res)[2] = (a)[2] + ((b)[2] - (a)[2]) * (t); \
	} while(0)

#define NORMALIZE(v)	\
	do { \
		float mag = (float)sqrt((v)[0] * (v)[0] + (v)[1] * (v)[1] + (v)[2] * (v)[2]); \
		(v)[0] /= mag; \
		(v)[1] /= mag; \
		(v)[2] /= mag; \
	} while(0)

static int clip_face(const Face &inface, float splitpos, int axis, int sign, Face *faces)
{
	assert(axis >= 0 && axis < 3);

	std::vector<Vertex> verts;
	bool clipped = false;

	for(int i=0; i<3; i++) {
		const Vertex *vstart = inface.v + i;
		const Vertex *vend = inface.v + ((i + 1) % 3);

		float start = vstart->pos[axis];
		float end = vend->pos[axis];

		if(OUTSIDE(start) && INSIDE(end)) {
			float t = (splitpos - start) / (end - start);

			Vertex newv;
			memset(&newv, 0, sizeof newv);
			LERPV3(newv.pos, vstart->pos, vend->pos, t);
			LERPV3(newv.normal, vstart->normal, vend->normal, t);
			LERPV3(newv.tex, vstart->tex, vend->tex, t);
			NORMALIZE(newv.normal);

			verts.push_back(newv);
			clipped = true;

		} else if(INSIDE(start) && INSIDE(end)) {
			verts.push_back(inface.v[i]);
		} else if(INSIDE(start) && OUTSIDE(end)) {
			verts.push_back(inface.v[i]);

			float t = (splitpos - start) / (end - start);

			Vertex newv;
			memset(&newv, 0, sizeof newv);
			LERPV3(newv.pos, vstart->pos, vend->pos, t);
			LERPV3(newv.normal, vstart->normal, vend->normal, t);
			LERPV3(newv.tex, vstart->tex, vend->tex, t);
			NORMALIZE(newv.normal);

			verts.push_back(newv);
			clipped = true;
		}
	}

	if(!clipped) {
		return 0;
	}

	assert(verts.size() < 5);
	bool quad = verts.size() > 3;

	if(!quad) {
		faces[0] = inface;
		faces[0].v[0] = verts[0];
		faces[0].v[1] = verts[1];
		faces[0].v[2] = verts[2];
		return 1;
	}

	/* calculate triangle areas for both possible splits and pick the one
	 * with the smallest absolute difference to avoid slivers.
	 */
	float area1, area2;

	area1 = calc_sq_area(verts[0].pos, verts[1].pos, verts[2].pos);
	area2 = calc_sq_area(verts[0].pos, verts[2].pos, verts[3].pos);
	float s1diff = fabs(area1 - area2);

	area1 = calc_sq_area(verts[0].pos, verts[1].pos, verts[3].pos);
	area2 = calc_sq_area(verts[1].pos, verts[2].pos, verts[3].pos);
	float s2diff = fabs(area1 - area2);

	faces[0] = faces[1] = inface;
	if(s1diff < s2diff) {
		faces[0].v[0] = verts[0];
		faces[0].v[1] = verts[1];
		faces[0].v[2] = verts[2];
		faces[1].v[0] = verts[0];
		faces[1].v[1] = verts[2];
		faces[1].v[2] = verts[3];
	} else {
		faces[0].v[0] = verts[0];
		faces[0].v[1] = verts[1];
		faces[0].v[2] = verts[3];
		faces[1].v[0] = verts[1];
		faces[1].v[1] = verts[2];
		faces[1].v[2] = verts[3];
	}
	return 2;
}

static float calc_sq_area(const Vector3 &a, const Vector3 &b, const Vector3 &c)
{
	Vector3 v1 = b - a;
	Vector3 v2 = c - a;
	return cross(v1, v2).lengthsq();
}
