#ifndef MESH_H_
#define MESH_H_

#include <stdio.h>
#include <vector>
#include <list>
#include "common.h"

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

struct Light {
	float pos[4], color[4];
};

class AABBox {
public:
	float min[4], max[4];

	float calc_surface_area() const;
};

struct KDNode {
	int axis;
	AABBox aabb;
	float cost;

	KDNode *left, *right;
	std::vector<int> face_idx;

	KDNode();
};

struct KDNodeGPU {
	AABBox aabb;
	int face_idx[MAX_NODE_FACES];
	int num_faces;
	int left, right;
	int padding;
};


class Scene {
private:
	mutable Face *facebuf;
	mutable int num_faces;

	mutable KDNodeGPU *kdbuf;

public:
	std::vector<Mesh*> meshes;
	std::vector<Light> lights;
	std::vector<Material> matlib;
	KDNode *kdtree;

	Scene();
	~Scene();

	bool add_mesh(Mesh *m);
	bool add_light(const Light &lt);

	int get_num_meshes() const;
	int get_num_lights() const;
	int get_num_faces() const;
	int get_num_materials() const;
	int get_num_kdnodes() const;

	Mesh **get_meshes();
	const Mesh * const *get_meshes() const;

	Light *get_lights();
	const Light *get_lights() const;

	Material *get_materials();
	const Material *get_materials() const;

	bool load(const char *fname);
	bool load(FILE *fp);

	const Face *get_face_buffer() const;
	const KDNodeGPU *get_kdtree_buffer() const;

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

bool kdtree_dump(const KDNode *tree, const char *fname);
KDNode *kdtree_restore(const char *fname);

#endif	/* MESH_H_ */
