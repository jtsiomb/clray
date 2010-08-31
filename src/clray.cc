#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#ifndef __APPLE__
#include <GL/glut.h>
#else
#include <GLUT/glut.h>
#endif
#include "rt.h"
#include "matrix.h"
#include "scene.h"
#include "ocl.h"
#include "ogl.h"

#ifdef _MSC_VER
#define snprintf	_snprintf
#endif

void cleanup();
void disp();
void reshape(int x, int y);
void keyb(unsigned char key, int x, int y);
void mouse(int bn, int status, int x, int y);
void motion(int x, int y);
bool capture(const char *namefmt);
bool write_ppm(const char *fname, float *fb, int xsz, int ysz);

static int xsz, ysz;
static bool need_update = true;

static float cam_theta, cam_phi = 25.0;
static float cam_dist = 10.0;

static bool dbg_glrender = false;
static bool dbg_show_kdtree = false;
static bool dbg_show_obj = true;
bool dbg_frame_time = true;

static Scene scn;
static unsigned int tex;

int main(int argc, char **argv)
{
	glutInitWindowSize(800, 600);
	glutInit(&argc, argv);

	int loaded = 0;
	for(int i=1; i<argc; i++) {
		if(argv[i][0] == '-' && argv[i][2] == 0) {
			switch(argv[i][1]) {
			case 'i':
				if(!argv[++i] || !isdigit(argv[i][0])) {
					fprintf(stderr, "-i must be followed by the intersection cost\n");
					return 1;
				}

				set_accel_param(ACCEL_PARAM_COST_INTERSECT, atoi(argv[i]));
				break;

			case 't':
				if(!argv[++i] || !isdigit(argv[i][0])) {
					fprintf(stderr, "-t must be followed by the traversal cost\n");
					return 1;
				}

				set_accel_param(ACCEL_PARAM_COST_TRAVERSE, atoi(argv[i]));
				break;

			case 'c':
				if(!argv[++i] || !isdigit(argv[i][0])) {
					fprintf(stderr, "-c must be followed by the max number of items per leaf node\n");
					return 1;
				}

				set_accel_param(ACCEL_PARAM_MAX_NODE_ITEMS, atoi(argv[i]));
				break;

			case 'd':
				dbg_glrender = true;
				break;

			default:
				fprintf(stderr, "unrecognized option: %s\n", argv[i]);
				return 1;
			}
		} else {
			if(!scn.load(argv[i])) {
				fprintf(stderr, "failed to load scene: %s\n", argv[i]);
				return false;
			}
			loaded++;
		}
	}

	if(!loaded) {
		fprintf(stderr, "you must specify a scene file to load\n");
		return false;
	}
	if(!scn.get_num_faces()) {
		fprintf(stderr, "didn't load any polygons\n");
		return false;
	}

	glutInitDisplayMode(GLUT_RGB | GLUT_DEPTH | GLUT_DOUBLE);
	glutCreateWindow("OpenCL Raytracer");

	xsz = glutGet(GLUT_WINDOW_WIDTH);
	ysz = glutGet(GLUT_WINDOW_HEIGHT);

	glutDisplayFunc(disp);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keyb);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);

	unsigned int *test_pattern = new unsigned int[xsz * ysz];
	for(int i=0; i<ysz; i++) {
		for(int j=0; j<xsz; j++) {
			test_pattern[i * xsz + j] = ((i >> 4) & 1) == ((j >> 4) & 1) ? 0xff0000 : 0xff00;
		}
	}

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, xsz, ysz, 0, GL_RGBA, GL_UNSIGNED_BYTE, test_pattern);
	delete [] test_pattern;

	if(!init_opencl()) {
		return 1;
	}

	if(!init_renderer(xsz, ysz, &scn, tex)) {
		return 1;
	}
	atexit(cleanup);

	glutMainLoop();
	return 0;
}

void cleanup()
{
	printf("destroying renderer ...\n");
	destroy_renderer();

	printf("shutting down OpenCL ...\n");
	destroy_opencl();

	printf("cleaning up OpenGL resources ...\n");
	glDeleteTextures(1, &tex);
}

static Matrix4x4 mat, inv_mat, inv_trans;

void disp()
{
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	if(need_update) {
		glPushMatrix();
		glRotatef(-cam_theta, 0, 1, 0);
		glRotatef(-cam_phi, 1, 0, 0);
		glTranslatef(0, 0, cam_dist);

		glGetFloatv(GL_MODELVIEW_MATRIX, mat.m);

		inv_mat = mat;
		inv_mat.invert();

		/*inv_trans = inv_mat;
		inv_trans.transpose();*/
		inv_trans = mat;
		inv_trans.m[3] = inv_trans.m[7] = inv_trans.m[11] = 0.0;
		inv_trans.m[12] = inv_trans.m[13] = inv_trans.m[14] = 0.0;
		inv_trans.m[15] = 1.0;

		set_xform(mat.m, inv_trans.m);
		glPopMatrix();

		if(!dbg_glrender) {
			if(!render()) {
				exit(1);
			}
			need_update = false;
		}
	}

	if(dbg_glrender) {
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glLoadMatrixf(inv_mat.m);
		dbg_render_gl(&scn, dbg_show_kdtree, dbg_show_obj);
	} else {
		glEnable(GL_TEXTURE_2D);
		glDisable(GL_LIGHTING);

		glBegin(GL_QUADS);
		glColor3f(1, 1, 1);
		glTexCoord2f(0, 1); glVertex2f(-1, -1);
		glTexCoord2f(1, 1); glVertex2f(1, -1);
		glTexCoord2f(1, 0); glVertex2f(1, 1);
		glTexCoord2f(0, 0); glVertex2f(-1, 1);
		glEnd();

		glDisable(GL_TEXTURE_2D);
	}

	glutSwapBuffers();
}

void reshape(int x, int y)
{
	glViewport(0, 0, x, y);

	/* reallocate the framebuffer */
	/*delete [] fb;
	fb = new float[x * y * 4];
	set_framebuffer(fb, x, y);*/
}

void idle()
{
	need_update = true;
	glutPostRedisplay();
}

void keyb(unsigned char key, int x, int y)
{
	switch(key) {
	case 27:
		exit(0);

	case '\b':
		{
			static bool busyloop;

			busyloop = !busyloop;
			printf("%s busy-looping\n", busyloop ? "WARNING: enabling" : "disabling");
			glutIdleFunc(busyloop ? idle : 0);
		}
		break;

	case 'd':
		dbg_glrender = !dbg_glrender;
		if(dbg_glrender) {
			printf("Debug OpenGL rendering\n");
		} else {
			printf("Raytracing\n");
		}
		glutPostRedisplay();
		break;

	case 'k':
		dbg_show_kdtree = !dbg_show_kdtree;
		if(dbg_glrender) {
			glutPostRedisplay();
		}
		break;

	case 'o':
		dbg_show_obj = !dbg_show_obj;
		if(dbg_glrender) {
			glutPostRedisplay();
		}
		break;

	case 's':
		{
			bool shadows = get_render_option_bool(ROPT_SHAD);
			shadows = !shadows;
			printf("%s shadows\n", shadows ? "enabling" : "disabling");
			set_render_option(ROPT_SHAD, shadows);
			need_update = true;
			glutPostRedisplay();
		}
		break;

	case 'r':
		{
			bool refl = get_render_option_bool(ROPT_REFL);
			refl = !refl;
			printf("%s reflections\n", refl ? "enabling" : "disabling");
			set_render_option(ROPT_REFL, refl);
			need_update = true;
			glutPostRedisplay();
		}
		break;

	case ']':
		{
			int iter = get_render_option_int(ROPT_ITER);
			printf("setting max iterations: %d\n", iter + 1);
			set_render_option(ROPT_ITER, iter + 1);
			need_update = true;
			glutPostRedisplay();
		}
		break;

	case '[':
		{
			int iter = get_render_option_int(ROPT_ITER);
			if(iter-- > 0) {
				printf("setting max iterations: %d\n", iter);
				set_render_option(ROPT_ITER, iter);
				need_update = true;
				glutPostRedisplay();
			}
		}
		break;

	case '`':
		capture("shot%03d.ppm");
		break;

	case 't':
		dbg_frame_time = !dbg_frame_time;
		break;

	default:
		break;
	}
}

static bool bnstate[32];
static int prev_x, prev_y;

void mouse(int bn, int state, int x, int y)
{
	if(state == GLUT_DOWN) {
		prev_x = x;
		prev_y = y;
		bnstate[bn] = true;
	} else {
		bnstate[bn] = false;
	}
}

#define ROT_SCALE	0.5
#define PAN_SCALE	0.1

void motion(int x, int y)
{
	int dx = x - prev_x;
	int dy = y - prev_y;
	prev_x = x;
	prev_y = y;

	if(bnstate[0]) {
		cam_theta += dx * ROT_SCALE;
		cam_phi += dy * ROT_SCALE;

		if(cam_phi < -89) cam_phi = -89;
		if(cam_phi > 89) cam_phi = 89;

		need_update = true;
		glutPostRedisplay();
	}
	if(bnstate[2]) {
		cam_dist += dy * PAN_SCALE;
		if(cam_dist < 0) cam_dist = 0;

		need_update = true;
		glutPostRedisplay();
	}
}

bool capture(const char *namefmt)
{
	static int num;
	char fname[256];

	num++;
	snprintf(fname, sizeof fname, namefmt, num);
	printf("saving image %s\n", fname);

	float *pixels = new float[4 * xsz * ysz];
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels);

	bool res = write_ppm("shot.ppm", pixels, xsz, ysz);
	if(!res) {
		num--;
	}
	delete [] pixels;
	return res;
}

bool write_ppm(const char *fname, float *fb, int xsz, int ysz)
{
	FILE *fp;

	if(!(fp = fopen(fname, "wb"))) {
		fprintf(stderr, "write_ppm: failed to open file %s for writing: %s\n", fname, strerror(errno));
		return false;
	}
	fprintf(fp, "P6\n%d %d\n255\n", xsz, ysz);

	for(int i=0; i<xsz * ysz * 4; i++) {
		if(i % 4 == 3) continue;

		unsigned char c = (unsigned char)(fb[i] * 255.0);
		fputc(c, fp);
	}
	fclose(fp);
	return true;
}
