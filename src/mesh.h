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

class Scene {
public:
	std::vector<Mesh*> meshes;
	std::vector<Material> matlib;

	bool load(const char *fname);
	bool load(FILE *fp);
};

#endif	/* MESH_H_ */
