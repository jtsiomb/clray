#include <string.h>
#include <assert.h>
#include "rt.h"
#include "ogl.h"

#define MIN(a, b)	((a) < (b) ? (a) : (b))
static void dbg_set_gl_material(Material *mat)
{
	static Material def_mat = {{0.7, 0.7, 0.7, 1}, {0, 0, 0, 0}, 0, 0, 0};

	if(!mat) mat = &def_mat;

	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, mat->kd);
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat->ks);
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, MIN(mat->spow, 128.0f));
}

void dbg_render_gl(Scene *scn, bool show_tree, bool show_obj)
{
	const RendInfo *rinf = get_render_info();
	const Face *faces = scn->get_face_buffer();

	glPushAttrib(GL_ENABLE_BIT | GL_TRANSFORM_BIT | GL_LIGHTING_BIT);

	for(int i=0; i<scn->get_num_lights(); i++) {
		float lpos[4];

		memcpy(lpos, scn->lights[i].pos, sizeof lpos);
		lpos[3] = 1.0;

		glLightfv(GL_LIGHT0 + i, GL_POSITION, lpos);
		glLightfv(GL_LIGHT0 + i, GL_DIFFUSE, scn->lights[i].color);
		glEnable(GL_LIGHT0 + i);
	}

	glDisable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_LIGHTING);

	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	gluPerspective(45.0, (float)rinf->xsz / (float)rinf->ysz, 0.5, 1000.0);

	if(show_obj) {
		Material *materials = scn->get_materials();

		int num_faces = scn->get_num_faces();
		int cur_mat = -1;

		for(int i=0; i<num_faces; i++) {
			if(faces[i].matid != cur_mat) {
				if(cur_mat != -1) {
					glEnd();
				}
				dbg_set_gl_material(materials ? materials + faces[i].matid : 0);
				cur_mat = faces[i].matid;
				glBegin(GL_TRIANGLES);
			}

			for(int j=0; j<3; j++) {
				glNormal3fv(faces[i].v[j].normal);
				glVertex3fv(faces[i].v[j].pos);
			}
		}
		glEnd();
	}

	if(show_tree) {
		scn->draw_kdtree();
	}

	glPopMatrix();
	glPopAttrib();

	assert(glGetError() == GL_NO_ERROR);
}
