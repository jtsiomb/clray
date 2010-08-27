#ifndef RT_H_
#define RT_H_

#include "scene.h"

bool init_renderer(int xsz, int ysz, Scene *scn, unsigned int tex);
void destroy_renderer();
bool render();
void set_xform(float *matrix, float *invtrans);

void dbg_render_gl(Scene *scn, bool show_tree = false, bool show_obj = true);

#endif	/* RT_H_ */
