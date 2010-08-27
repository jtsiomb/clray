#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <assert.h>
#include <map>
#include "scene.h"
#include "ogl.h"


#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) > (b) ? (a) : (b))


static void flatten_kdtree(const KDNode *node, KDNodeGPU *kdbuf, int *count, std::map<const KDNode*, int> *flatmap);
static void fix_child_links(const KDNode *node, KDNodeGPU *kdbuf, const std::map<const KDNode*, int> &flatmap);
static void draw_kdtree(const KDNode *node, int level = 0);
static bool build_kdtree(KDNode *kd, const Face *faces, int level = 0);
static float eval_cost(const Face *faces, const int *face_idx, int num_faces, const AABBox &aabb, int axis);
static void free_kdtree(KDNode *node);
static void print_item_counts(const KDNode *node, int level);


static int accel_param[NUM_ACCEL_PARAMS] = {
	40,	// max tree depth
	0,	// max items per node (0 means ignore limit)
	5,	// estimated traversal cost
	15	// estimated interseciton cost
};


void set_accel_param(int p, int v)
{
	assert(p >= 0 && p < NUM_ACCEL_PARAMS);
	accel_param[p] = v;
}


#define FEQ(a, b)	(fabs((a) - (b)) < 1e-8)
bool Face::operator ==(const Face &f) const
{
	for(int i=0; i<3; i++) {
		for(int j=0; j<3; j++) {
			if(!FEQ(v[i].pos[j], f.v[i].pos[j])) {
				return false;
			}
			if(!FEQ(v[i].normal[j], f.v[i].normal[j])) {
				return false;
			}
		}
		if(!FEQ(normal[i], f.normal[i])) {
			return false;
		}
	}
	return true;
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

int Scene::get_num_meshes() const
{
	return (int)meshes.size();
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

static int find_node_index(const KDNode *node, const std::map<const KDNode*, int> &flatmap);

static bool validate_flat_tree(const KDNode *node, const KDNodeGPU *kdbuf, int count, const std::map<const KDNode*, int> &flatmap)
{
	if(!node) return true;

	int idx = find_node_index(node, flatmap);
	int left_idx = find_node_index(node->left, flatmap);
	int right_idx = find_node_index(node->right, flatmap);

	if(kdbuf[idx].left != left_idx) {
		fprintf(stderr, "%d's left should be %d. it's: %d\n", idx, left_idx, kdbuf[idx].left);
		return false;
	}
	if(kdbuf[idx].right != right_idx) {
		fprintf(stderr, "%d's right should be %d. it's: %d\n", idx, right_idx, kdbuf[idx].right);
		return false;
	}
	return validate_flat_tree(node->left, kdbuf, count, flatmap) && validate_flat_tree(node->right, kdbuf, count, flatmap);
}

const KDNodeGPU *Scene::get_kdtree_buffer() const
{
	if(kdbuf) {
		return kdbuf;
	}

	if(!kdtree) {
		((Scene*)this)->build_kdtree();
	}

	/* we'll associate kdnodes with flattened array indices through this map as
	 * we add them so that the second child-index pass, will be able to find
	 * the children indices of each node.
	 */
	std::map<const KDNode*, int> flatmap;
	flatmap[0] = -1;

	int num_nodes = get_num_kdnodes();
	kdbuf = new KDNodeGPU[num_nodes];

	int count = 0;

	// first arrange the kdnodes into an array (flatten)
	flatten_kdtree(kdtree, kdbuf, &count, &flatmap);

	// then fix all the left/right links to point to the correct nodes
	fix_child_links(kdtree, kdbuf, flatmap);

	assert(validate_flat_tree(kdtree, kdbuf, count, flatmap));

	return kdbuf;
}

static void flatten_kdtree(const KDNode *node, KDNodeGPU *kdbuf, int *count, std::map<const KDNode*, int> *flatmap)
{
	int idx = (*count)++;

	// copy the node
	kdbuf[idx].aabb = node->aabb;
	for(size_t i=0; i<node->face_idx.size(); i++) {
		kdbuf[idx].face_idx[i] = node->face_idx[i];
	}
	kdbuf[idx].num_faces = node->face_idx.size();

	// update the node* -> array-position mapping
	(*flatmap)[node] = idx;

	// recurse to the left/right (if we're not in a leaf node)
	if(node->left) {
		assert(node->right);

		flatten_kdtree(node->left, kdbuf, count, flatmap);
		flatten_kdtree(node->right, kdbuf, count, flatmap);
	}
}

static int find_node_index(const KDNode *node, const std::map<const KDNode*, int> &flatmap)
{
	std::map<const KDNode*, int>::const_iterator it = flatmap.find(node);
	assert(it != flatmap.end());
	return it->second;
}

static void fix_child_links(const KDNode *node, KDNodeGPU *kdbuf, const std::map<const KDNode*, int> &flatmap)
{
	if(!node) return;

	int idx = find_node_index(node, flatmap);

	kdbuf[idx].left = find_node_index(node->left, flatmap);
	if(!node->left && kdbuf[idx].left != -1) abort();
	kdbuf[idx].right = find_node_index(node->right, flatmap);
	if(!node->right && kdbuf[idx].right != -1) abort();

	fix_child_links(node->left, kdbuf, flatmap);
	fix_child_links(node->right, kdbuf, flatmap);
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
	/*if(!node->left) return;

	AABBox *bleft = &node->left->aabb;

	int axis = level % 3;
	switch(axis) {
	case 0:
		glVertex3f(bleft->max[0], bleft->min[1], bleft->min[2]);
		glVertex3f(bleft->max[0], bleft->max[1], bleft->min[2]);
		glVertex3f(bleft->max[0], bleft->max[1], bleft->min[2]);
		glVertex3f(bleft->max[0], bleft->max[1], bleft->max[2]);
		glVertex3f(bleft->max[0], bleft->max[1], bleft->max[2]);
		glVertex3f(bleft->max[0], bleft->min[1], bleft->max[2]);
		glVertex3f(bleft->max[0], bleft->min[1], bleft->max[2]);
		glVertex3f(bleft->max[0], bleft->min[1], bleft->min[2]);
		break;

	case 1:
		glVertex3f(bleft->min[0], bleft->min[1], bleft->max[2]);
		glVertex3f(bleft->min[0], bleft->max[1], bleft->max[2]);
		glVertex3f(bleft->min[0], bleft->max[1], bleft->max[2]);
		glVertex3f(bleft->max[0], bleft->max[1], bleft->max[2]);
		glVertex3f(bleft->max[0], bleft->max[1], bleft->max[2]);
		glVertex3f(bleft->max[0], bleft->min[1], bleft->max[2]);
		glVertex3f(bleft->max[0], bleft->min[1], bleft->max[2]);
		glVertex3f(bleft->min[0], bleft->min[1], bleft->max[2]);
		break;

	case 2:
		glVertex3f(bleft->min[0], bleft->max[1], bleft->min[2]);
		glVertex3f(bleft->max[0], bleft->max[1], bleft->min[2]);
		glVertex3f(bleft->max[0], bleft->max[1], bleft->min[2]);
		glVertex3f(bleft->max[0], bleft->max[1], bleft->max[2]);
		glVertex3f(bleft->max[0], bleft->max[1], bleft->max[2]);
		glVertex3f(bleft->min[0], bleft->max[1], bleft->max[2]);
		glVertex3f(bleft->min[0], bleft->max[1], bleft->max[2]);
		glVertex3f(bleft->min[0], bleft->max[1], bleft->min[2]);
		break;

	default:
		break;
	}*/
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
	best_split.sum_cost = FLT_MAX;

	for(int i=0; i<1; i++) {
		Split split;
		find_best_split(kd, i, faces, &split);

		if(split.sum_cost < best_split.sum_cost) {
			best_split = split;
		}
	}

	kd->axis = best_split.axis;

	//printf("current cost: %f,   best_cost: %f\n", kd->cost, best_sum_cost);
	if(best_split.sum_cost > kd->cost && (opt_max_items == 0 || (int)kd->face_idx.size() <= opt_max_items)) {
		return true;	// stop splitting if it doesn't reduce the cost
	}

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

	float sarea = aabb.calc_surface_area();
	if(sarea < 1e-6) {
		return FLT_MAX;	// heavily penalize 0-area voxels
	}

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
