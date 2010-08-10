#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifndef __APPLE__
#include <GL/glut.h>
#else
#include <GLUT/glut.h>
#endif
#include "rt.h"
#include "matrix.h"
#include "mesh.h"

void cleanup();
void disp();
void reshape(int x, int y);
void keyb(unsigned char key, int x, int y);
void mouse(int bn, int status, int x, int y);
void motion(int x, int y);
bool write_ppm(const char *fname, float *fb, int xsz, int ysz);

static int xsz, ysz;
static bool need_update = true;

static float cam_theta, cam_phi = 25.0;
static float cam_dist = 10.0;

static bool dbg_glrender = true;

static Scene scn;

int main(int argc, char **argv)
{
	glutInitWindowSize(800, 600);
	glutInit(&argc, argv);

	int loaded = 0;
	for(int i=1; i<argc; i++) {
		if(!scn.load(argv[i])) {
			fprintf(stderr, "failed to load scene: %s\n", argv[i]);
			return false;
		}
		loaded++;
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

	if(!init_renderer(xsz, ysz, &scn)) {
		return 1;
	}
	atexit(cleanup);

	/*glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);*/
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, 4, xsz, ysz, 0, GL_RGBA, GL_FLOAT, 0);

	glutMainLoop();
	return 0;
}

void cleanup()
{
	destroy_renderer();
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
		dbg_render_gl(&scn);
	} else {
		glEnable(GL_TEXTURE_2D);

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

void keyb(unsigned char key, int x, int y)
{
	switch(key) {
	case 27:
		exit(0);

	case 'r':
		need_update = true;
		glutPostRedisplay();
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
