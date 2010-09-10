#ifndef RT_H_
#define RT_H_

#include "scene.h"

enum {
	ROPT_ITER,
	ROPT_SHAD,
	ROPT_REFL,

	NUM_RENDER_OPTIONS
};

struct RendInfo {
	float ambient[4];
	int xsz, ysz;
	int num_faces, num_lights;
	int max_iter;
	int cast_shadows;
};

struct Ray {
	float origin[4], dir[4];
};


bool init_renderer(int xsz, int ysz, Scene *scn, unsigned int tex);
void destroy_renderer();
bool render();
void set_xform(float *matrix, float *invtrans);

const RendInfo *get_render_info();

void set_render_option(int opt, bool val);
void set_render_option(int opt, int val);
void set_render_option(int opt, float val);

bool get_render_option_bool(int opt);
int get_render_option_int(int opt);
float get_render_option_float(int opt);

// raytrace in the CPU
bool init_dbg_renderer(int xsz, int ysz, Scene *scn, unsigned int texid);
void destroy_dbg_renderer();
void dbg_set_primary_rays(const Ray *rays);
void dbg_render(const float *xform, const float *invtrans_xform, int num_threads = -1);

// visualize the scene using OpenGL
void dbg_render_gl(Scene *scn, bool show_tree = false, bool show_obj = true);

#endif	/* RT_H_ */
