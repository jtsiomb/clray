#ifndef RT_H_
#define RT_H_

#include "mesh.h"

bool init_renderer(int xsz, int ysz, Scene *scn);
void destroy_renderer();
bool render();
void set_xform(float *matrix, float *invtrans);

void dbg_set_dbg(int dbg);
void dbg_render_gl(Scene *scn);

#endif	/* RT_H_ */
