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

#define KDCLEAR(node)	((node)->axis = -1)
#define KDUSED(node)	((node)->axis >= 0)
#define KDPARENT(x)		((x) >> 1)
#define KDLEFT(x)		((x) << 1)
#define KDRIGHT(x)		(((x) << 1) + 1)

struct KDNode {
	int axis;
	float pt;
	AABBox aabb;

	KDNode *left, *right;
	std::list<const Face*> faces;
};

struct KDNodeGPU {
	int axis;
	float pt;
};


class Scene {
private:
	mutable Face *facebuf;
	mutable int num_faces;

public:
	std::vector<Mesh*> meshes;
	std::vector<Material> matlib;

	KDNode *kdtree;
	std::vector<KDNode> kdtree_gpu;

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
	void build_kdtree();
};

#endif	/* MESH_H_ */
