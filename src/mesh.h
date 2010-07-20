#ifndef MESH_H_
#define MESH_H_

#include <vector>

struct Vertex {
	float pos[3];
	float normal[3];
	float tex[2];
};

struct Face {
	Vertex v[3];
	float normal[3];
	int matid;
};

struct Material {
	float kd[3], ks[3];
	float kr, kt;
	float spow;
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
