#ifndef RT_H_
#define RT_H_

bool init_renderer(int xsz, int ysz);
void destroy_renderer();
bool render();
void set_xform(float *matrix, float *invtrans);

void dbg_render_gl();

#endif	/* RT_H_ */
