#ifndef RT_H_
#define RT_H_

bool init_renderer(int xsz, int ysz, float *fb);
void destroy_renderer();
bool render();
void set_xform(float *matrix);

#endif	/* RT_H_ */
