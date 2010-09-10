#ifndef COMMON_H_
#define COMMON_H_

/* error threshold */
#define EPSILON		1e-5

/* minimum trace energy threshold */
#define MIN_ENERGY	0.001

/* primary ray magnitude */
#define RAY_MAG		500.0

/* maximum faces per leaf node of the kd-tree */
#define MAX_NODE_FACES		32

/* maximum kdtree depth */
#define MAX_TREE_DEPTH		64

/* width in pixels of the image-ified kdtree node */
#define KDIMG_NODE_WIDTH	16

/* maximum kdtree image height */
#define KDIMG_MAX_HEIGHT	4096

#endif	/* COMMON_H_ */
