#include <math.h>
#include <float.h>
#include <assert.h>
#include "scene.h"


static int build_kdtree(KDNode *kd);
static float eval_cost(const std::list<const Face*> &faces, const AABBox &aabb, int axis);
static void free_kdtree(KDNode *node);


static int accel_param[NUM_ACCEL_PARAMS] = {
	75,	// max tree depth
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
	axis = 0;
	pt = 0.0;
	left = right = 0;
	num_faces = 0;
}


Scene::Scene()
{
	facebuf = 0;
	num_faces = -1;
	kdtree = 0;
}

Scene::~Scene()
{
	delete [] facebuf;
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


void Scene::build_kdtree()
{
	const Face *faces = get_face_buffer();
	int num_faces = get_num_faces();

	printf("Constructing kd-tree out of %d faces ...\n", num_faces);

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

		kdtree->faces.push_back(face);	// add the face
		kdtree->num_faces++;
	}

	// calculate the heuristic for the root
	kdtree->cost = eval_cost(kdtree->faces, kdtree->aabb, kdtree->axis);

	// now proceed splitting the root recursively
	::build_kdtree(kdtree);
}

static int build_kdtree(KDNode *kd)
{
	int opt_max_items = accel_param[ACCEL_PARAM_MAX_NODE_ITEMS];
	if(kd->num_faces == 0 || (opt_max_items > 1 && kd->num_faces < opt_max_items)) {
		return 0;
	}

	int axis = (kd->axis + 1) % 3;

	float best_cost[2], best_sum_cost = FLT_MAX;
	float best_split;

	std::list<const Face*>::iterator it = kd->faces.begin();
	while(it != kd->faces.end()) {
		const Face *face = *it++;

		for(int i=0; i<3; i++) {
			AABBox aabb_left, aabb_right;
			const float *split = face->v[i].pos;

			aabb_left = aabb_right = kd->aabb;
			aabb_left.max[axis] = split[axis];
			aabb_right.min[axis] = split[axis];

			float left_cost = eval_cost(kd->faces, aabb_left, axis);
			float right_cost = eval_cost(kd->faces, aabb_right, axis);
			float sum_cost = left_cost + right_cost;

			if(sum_cost < best_sum_cost) {
				best_cost[0] = left_cost;
				best_cost[1] = right_cost;
				best_sum_cost = sum_cost;
				best_split = split[axis];
			}
		}
	}

	if(best_sum_cost >= kd->cost) {
		return 0;	// stop splitting if it doesn't reduce the cost
	}
	kd->pt = best_split;

	// create the two children
	KDNode *kdleft, *kdright;
	kdleft = new KDNode;
	kdright = new KDNode;

	kdleft->axis = kdright->axis = axis;
	kdleft->aabb = kdright->aabb = kd->aabb;

	kdleft->aabb.max[axis] = best_split;
	kdright->aabb.min[axis] = best_split;

	kdleft->cost = best_cost[0];
	kdright->cost = best_cost[1];

	it = kd->faces.begin();
	while(it != kd->faces.end()) {
		const Face *face = *it++;

		if(face->v[0].pos[axis] < best_split ||
				face->v[1].pos[axis] < best_split ||
				face->v[2].pos[axis] < best_split) {
			kdleft->faces.push_back(face);
			kdleft->num_faces++;
		}
		if(face->v[0].pos[axis] >= best_split ||
				face->v[1].pos[axis] >= best_split ||
				face->v[2].pos[axis] >= best_split) {
			kdright->faces.push_back(face);
			kdright->num_faces++;
		}
	}

	kd->left = kdleft;
	kd->right = kdright;
	return 0;
}

static float eval_cost(const std::list<const Face*> &faces, const AABBox &aabb, int axis)
{
	int num_inside = 0;
	int tcost = accel_param[ACCEL_PARAM_COST_TRAVERSE];
	int icost = accel_param[ACCEL_PARAM_COST_INTERSECT];

	std::list<const Face*>::const_iterator it = faces.begin();
	while(it != faces.end()) {
		const Face *face = *it++;

		for(int i=0; i<3; i++) {
			if(face->v[i].pos[axis] >= aabb.min[axis] && face->v[i].pos[axis] < aabb.max[axis]) {
				num_inside++;
				break;
			}
		}
	}

	return tcost + aabb.calc_surface_area() * num_inside * icost;
}

static void free_kdtree(KDNode *node)
{
	if(node) {
		free_kdtree(node->left);
		free_kdtree(node->right);
		delete node;
	}
}
