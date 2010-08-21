#ifndef MESH_H_
#define MESH_H_

#include <stdio.h>
#include <vector>
#include <list>

struct Vertex {
	float pos[4];
	float normal[4];
	float tex[4];
	float padding[4];
};

struct Face {
	Vertex v[3];
	float normal[4];
	int matid;
	int padding[3];

	bool operator ==(const Face &f) const;
};

struct Material {
	float kd[4], ks[4];
	float kr, kt;
	float spow;
	float padding;
};

struct Mesh {
	std::vector<Face> faces;
	int matid;
};

class AABBox {
public:
	float min[4], max[4];

	float calc_surface_area() const;
};

enum {
	KDAXIS_X,
	KDAXIS_Y,
	KDAXIS_Z
};

struct KDNode {
	int axis;
	float pt;
	AABBox aabb;
	float cost;

	KDNode *left, *right;
	int num_faces;	// cause on some implementations list::size() is O(n)
	std::list<const Face*> faces;

	KDNode();
};

struct KDNodeGPU {
	AABBox aabb;
	int face_idx[32];
	int num_faces;
	int padding[3];
};


class Scene {
private:
	mutable Face *facebuf;
	mutable int num_faces;

	mutable KDNodeGPU *kdbuf;
	mutable int num_kdnodes;

public:
	std::vector<Mesh*> meshes;
	std::vector<Material> matlib;
	KDNode *kdtree;

	Scene();
	~Scene();

	bool add_mesh(Mesh *m);
	int get_num_meshes() const;
	int get_num_materials() const;
	int get_num_faces() const;

	Material *get_materials();
	const Material *get_materials() const;

	bool load(const char *fname);
	bool load(FILE *fp);

	const Face *get_face_buffer() const;
	const KDNodeGPU *get_kdtree_buffer() const;
	int get_num_kdnodes() const;

	void draw_kdtree() const;
	bool build_kdtree();
};

enum {
	ACCEL_PARAM_MAX_TREE_DEPTH,
	ACCEL_PARAM_MAX_NODE_ITEMS,
	ACCEL_PARAM_COST_TRAVERSE,
	ACCEL_PARAM_COST_INTERSECT,

	NUM_ACCEL_PARAMS
};

void set_accel_param(int p, int v);

int kdtree_depth(const KDNode *tree);
int kdtree_nodes(const KDNode *tree);

#endif	/* MESH_H_ */
