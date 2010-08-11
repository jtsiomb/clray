#ifndef MESH_H_
#define MESH_H_

#include <vector>

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

/*enum {
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
};*/

class Scene {
public:
	std::vector<Mesh*> meshes;
	std::vector<Material> matlib;
	//std::vector<KDNode> kdtree;

	bool add_mesh(Mesh *m);
	int get_num_meshes() const;
	int get_num_materials() const;
	int get_num_faces() const;

	Material *get_materials();
	const Material *get_materials() const;

	bool load(const char *fname);
	bool load(FILE *fp);

	//void build_kdtree();
};

#endif	/* MESH_H_ */
