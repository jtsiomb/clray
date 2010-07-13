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

void cleanup();
void disp();
void reshape(int x, int y);
void keyb(unsigned char key, int x, int y);
void mouse(int bn, int status, int x, int y);
void motion(int x, int y);
bool write_ppm(const char *fname, float *fb, int xsz, int ysz);

static float *fb;
static int xsz, ysz;
static bool need_update = true;

int main(int argc, char **argv)
{
	glutInitWindowSize(800, 600);
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
	glutCreateWindow("OpenCL Raytracer");

	xsz = glutGet(GLUT_WINDOW_WIDTH);
	ysz = glutGet(GLUT_WINDOW_HEIGHT);

	glutDisplayFunc(disp);
	glutReshapeFunc(reshape);
	glutKeyboardFunc(keyb);
	glutMouseFunc(mouse);
	glutMotionFunc(motion);

	fb = new float[xsz * ysz * 4];
	if(!init_renderer(xsz, ysz, fb)) {
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
	delete [] fb;
	destroy_renderer();
}

void disp()
{
	if(need_update) {
		render();
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, xsz, ysz, GL_RGBA, GL_FLOAT, fb);
		need_update = false;
	}

	glEnable(GL_TEXTURE_2D);

	glBegin(GL_QUADS);
	glTexCoord2f(0, 1); glVertex2f(-1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, -1);
	glTexCoord2f(1, 0); glVertex2f(1, 1);
	glTexCoord2f(0, 0); glVertex2f(-1, 1);
	glEnd();

	glDisable(GL_TEXTURE_2D);

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

	case 's':
		if(write_ppm("shot.ppm", fb, xsz, ysz)) {
			printf("captured screenshot shot.ppm\n");
		}
		break;

	case 'r':
		need_update = true;
		glutPostRedisplay();
		break;

	default:
		break;
	}
}

void mouse(int bn, int state, int x, int y)
{
}

void motion(int x, int y)
{
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
