#ifndef RT_H_
#define RT_H_

#include "scene.h"

enum {
	ROPT_ITER,
	ROPT_SHAD,
	ROPT_REFL,

	NUM_RENDER_OPTIONS
};

bool init_renderer(int xsz, int ysz, Scene *scn, unsigned int tex);
void destroy_renderer();
bool render();
void set_xform(float *matrix, float *invtrans);

void set_render_option(int opt, bool val);
void set_render_option(int opt, int val);
void set_render_option(int opt, float val);

bool get_render_option_bool(int opt);
int get_render_option_int(int opt);
float get_render_option_float(int opt);

void dbg_render_gl(Scene *scn, bool show_tree = false, bool show_obj = true);

#endif	/* RT_H_ */
