#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <string>
#include <vector>
#include <map>
#include "mesh.h"

using namespace std;

#define COMMANDS	\
	CMD(V),			\
	CMD(VN),		\
	CMD(VT),		\
	CMD(F),			\
	CMD(O),			\
	CMD(G),			\
	CMD(MTLLIB),	\
	CMD(USEMTL),	\
	CMD(NEWMTL),	\
	CMD(KA),		\
	CMD(KD),		\
	CMD(KS),		\
	CMD(NS),		\
	CMD(NI),		\
	CMD(D),			\
	CMD(TR),		\
	CMD(MAP_KD),	\
	CMD(MAP_KS),	\
	CMD(MAP_NS),	\
	CMD(MAP_D),		\
	CMD(REFL),		\
	CMD(BUMP)

#define CMD(x)	CMD_##x
enum {
	COMMANDS,
	CMD_UNK
};
#undef CMD

#define CMD(x)	#x
static const char *cmd_names[] = {
	COMMANDS,
	0
};
#undef CMD

struct Vector3 {
	float x, y, z;

	Vector3() { x = y = z = 0.0; }
	Vector3(float a, float b, float c) { x = a; y = b; z = c; }
};

struct Vector2 {
	float x, y;

	Vector2() { x = y = 0.0; }
	Vector2(float a, float b) { x = a; y = b; }
};

struct obj_face {
	int elem;
	int v[4], n[4], t[4];
};

struct obj_file {
	string cur_obj, cur_mat;
	vector<Vector3> v, vn, vt;
	vector<obj_face> f;
};

struct obj_mat {
	string name;		// newmtl <name>
	Vector3 ambient, diffuse, specular;	// Ka, Kd, Ks
	float shininess;	// Ns
	float ior;			// Ni
	float alpha;		// d, Tr

	string tex_dif, tex_spec, tex_shin, tex_alpha;	// map_Kd, map_Ks, map_Ns, map_d
	string tex_refl;	// refl -type sphere|cube file
	string tex_bump;	// bump

	obj_mat() { reset(); }

	void reset() {
		ambient = diffuse = Vector3(0.5, 0.5, 0.5);
		specular = Vector3(0.0, 0.0, 0.0);
		name = tex_dif = tex_spec = tex_shin = tex_alpha = tex_refl = tex_bump = "";
		shininess = 0;
		ior = alpha = 1;
	}
};

static bool read_materials(FILE *fp, vector<obj_mat> *vmtl);
static Mesh *cons_mesh(obj_file *obj);

static int get_cmd(char *str);
static bool is_int(const char *str);
static bool is_float(const char *str);
static bool parse_vec(Vector3 *vec);
static bool parse_color(Vector3 *col);
static bool parse_face(obj_face *face);
static const char *parse_map();

static bool find_file(char *res, int sz, const char *fname, const char *path = ".", const char *mode = "rb");
static const char *dirname(const char *str);

static map<string, int> matnames;


#define INVALID_IDX		INT_MIN

#define SEP		" \t\n\r\v"
#define BUF_SZ	512

bool Scene::load(const char *fname)
{
	FILE *fp;

	if(!(fp = fopen(fname, "rb"))) {
		fprintf(stderr, "failed to open %s: %s\n", fname, strerror(errno));
		return false;
	}

	bool res = load(fp);
	fclose(fp);
	return res;
}

bool Scene::load(FILE *fp)
{
	static int seq;
	char cur_name[16];

	obj_file obj;

	sprintf(cur_name, "default%02d.obj", seq++);
	obj.cur_obj = cur_name;

	int prev_cmd = 0, obj_added = 0;
	for(;;) {
		Vector3 vec;
		obj_face face;

		char line[BUF_SZ];
		fgets(line, sizeof line, fp);
		if(feof(fp)) {
			break;
		}

		char *tok;
		if(!(tok = strtok(line, SEP))) {
			continue; // ignore empty lines
		}

		int cmd;
		if((cmd = get_cmd(tok)) == -1) {
			continue; // ignore unknown commands ...
		}

		switch(cmd) {
		case CMD_V:
			if(!parse_vec(&vec)) {
				continue;
			}
			obj.v.push_back(vec);
			break;

		case CMD_VN:
			if(!parse_vec(&vec)) {
				continue;
			}
			obj.vn.push_back(vec);
			break;

		case CMD_VT:
			if(!parse_vec(&vec)) {
				continue;
			}
			vec.y = 1.0 - vec.y;
			obj.vt.push_back(vec);
			break;

		case CMD_O:
		case CMD_G:
			if(prev_cmd == CMD_O || prev_cmd == CMD_G) {
				break;	// just in case we've got both of them in a row
			}
			/* if we have any previous data, group them up, add the object
			 * and continue with the new one...
			 */
			if(!obj.f.empty()) {
				Mesh *mesh = cons_mesh(&obj);
				mesh->matid = matnames[obj.cur_mat];
				meshes.push_back(mesh);
				obj_added++;

				obj.f.clear();	// clean the face list
			}
			if((tok = strtok(0, SEP))) {
				obj.cur_obj = tok;
			} else {
				sprintf(cur_name, "default%02d.obj", seq++);
				obj.cur_obj = cur_name;
			}
			break;

		case CMD_MTLLIB:
			if((tok = strtok(0, SEP))) {
				char path[PATH_MAX];

				sprintf(path, ".:%s", dirname(tok));
				if(!find_file(path, PATH_MAX, tok, path)) {
					fprintf(stderr, "material library not found: %s\n", tok);
					continue;
				}

				FILE *mfile;
				if(!(mfile = fopen(path, "rb"))) {
					fprintf(stderr, "failed to open material library: %s\n", path);
					continue;
				}

				// load all materials of the mtl file into a vector
				vector<obj_mat> vmtl;
				if(!read_materials(mfile, &vmtl)) {
					continue;
				}
				fclose(mfile);

				// and add them all to the scene
				for(size_t i=0; i<vmtl.size(); i++) {
					Material mat;

					mat.kd[0] = vmtl[i].diffuse.x;
					mat.kd[1] = vmtl[i].diffuse.y;
					mat.kd[2] = vmtl[i].diffuse.z;

					mat.ks[0] = vmtl[i].specular.x;
					mat.ks[1] = vmtl[i].specular.y;
					mat.ks[2] = vmtl[i].specular.z;

					mat.kt = 1.0 - vmtl[i].alpha;
					mat.kr = 0.0;	// TODO
					mat.spow = vmtl[i].shininess;

					matnames[vmtl[i].name] = i;
				}
			}
			break;

		case CMD_USEMTL:
			if((tok = strtok(0, SEP))) {
				obj.cur_mat = tok;
			} else {
				obj.cur_mat = "";
			}
			break;

		case CMD_F:
			if(!parse_face(&face)) {
				continue;
			}

			// convert negative indices to regular indices
			for(int i=0; i<4; i++) {
				if(face.v[i] < 0 && face.v[i] != INVALID_IDX) {
					face.v[i] = obj.v.size() + face.v[i];
				}
				if(face.n[i] < 0 && face.n[i] != INVALID_IDX) {
					face.n[i] = obj.vn.size() + face.n[i];
				}
				if(face.t[i] < 0 && face.t[i] != INVALID_IDX) {
					face.t[i] = obj.vt.size() + face.t[i];
				}
			}

			// break quads into triangles if needed
			obj.f.push_back(face);
			if(face.elem == 4) {
				face.v[1] = face.v[2];
				face.n[1] = face.n[2];
				face.t[1] = face.t[2];

				face.v[2] = face.v[3];
				face.n[2] = face.n[3];
				face.t[2] = face.t[3];

				obj.f.push_back(face);
			}
			break;

		default:
			break;	// ignore unknown commands
		}

		prev_cmd = cmd;
	}

	// reached end of file...
	if(!obj.f.empty()) {
		Mesh *mesh = cons_mesh(&obj);
		mesh->matid = matnames[obj.cur_mat];
		meshes.push_back(mesh);
		obj_added++;
	}

	return obj_added > 0;
}

static Mesh *cons_mesh(obj_file *obj)
{
	Mesh *mesh;

	// need at least one of each element
	bool added_norm = false, added_tc = false;
	if(obj->vn.empty()) {
		obj->vn.push_back(Vector3(0, 0, 0));
		added_norm = true;
	}
	if(obj->vt.empty()) {
		obj->vt.push_back(Vector3(0, 0, 0));
		added_tc = true;
	}

	mesh = new Mesh;

	for(size_t i=0; i<obj->f.size(); i++) {
		Face face;

		for(int j=0; j<3; j++) {
			obj_face *f = &obj->f[i];

			face.v[j].pos[0] = obj->v[f->v[j]].x;
			face.v[j].pos[1] = obj->v[f->v[j]].y;
			face.v[j].pos[2] = obj->v[f->v[j]].z;

			int nidx = f->n[j] < 0 ? 0 : f->n[j];
			face.v[j].normal[0] = obj->vn[nidx].x;
			face.v[j].normal[1] = obj->vn[nidx].y;
			face.v[j].normal[2] = obj->vn[nidx].z;

			int tidx = f->t[j] < 0 ? 0 : f->t[j];
			face.v[j].tex[0] = obj->vt[tidx].x;
			face.v[j].tex[1] = obj->vt[tidx].y;
		}
		mesh->faces.push_back(face);
	}

	if(added_norm) {
		obj->vn.pop_back();
	}
	if(added_tc) {
		obj->vt.pop_back();
	}

	return mesh;
}

static bool read_materials(FILE *fp, vector<obj_mat> *vmtl)
{
	obj_mat mat;

	for(;;) {
		char line[BUF_SZ];
		fgets(line, sizeof line, fp);
		if(feof(fp)) {
			break;
		}

		char *tok;
		if(!(tok = strtok(line, SEP))) {
			continue;
		}

		int cmd;
		if((cmd = get_cmd(tok)) == -1) {
			continue;
		}

		switch(cmd) {
		case CMD_NEWMTL:
			// add the previous material, and start a new one
			if(mat.name.length() > 0) {
				vmtl->push_back(mat);
				mat.reset();
			}
			if((tok = strtok(0, SEP))) {
				mat.name = tok;
			}
			break;

		case CMD_KA:
			parse_color(&mat.ambient);
			break;

		case CMD_KD:
			parse_color(&mat.diffuse);
			break;

		case CMD_KS:
			parse_color(&mat.specular);
			break;

		case CMD_NS:
			if((tok = strtok(0, SEP)) && is_float(tok)) {
				mat.shininess = atof(tok);
			}
			break;

		case CMD_NI:
			if((tok = strtok(0, SEP)) && is_float(tok)) {
				mat.ior = atof(tok);
			}
			break;

		case CMD_D:
		case CMD_TR:
			{
				Vector3 c;
				if(parse_color(&c)) {
					mat.alpha = cmd == CMD_D ? c.x : 1.0 - c.x;
				}
			}
			break;

		case CMD_MAP_KD:
			mat.tex_dif = parse_map();
			break;

		default:
			break;
		}
	}

	if(mat.name.length() > 0) {
		vmtl->push_back(mat);
	}
	return true;
}

static int get_cmd(char *str)
{
	char *s = str;
	while((*s = toupper(*s))) s++;

	for(int i=0; cmd_names[i]; i++) {
		if(strcmp(str, cmd_names[i]) == 0) {
			return i;
		}
	}
	return CMD_UNK;
}

static bool is_int(const char *str)
{
	char *tmp;
	strtol(str, &tmp, 10);
	return tmp != str;
}

static bool is_float(const char *str)
{
	char *tmp;
	strtod(str, &tmp);
	return tmp != str;
}

static bool parse_vec(Vector3 *vec)
{
	for(int i=0; i<3; i++) {
		char *tok;

		if(!(tok = strtok(0, SEP)) || !is_float(tok)) {
			if(i < 2) {
				return false;
			}
			vec->z = 0.0;
		} else {
			float v = atof(tok);

			switch(i) {
			case 0:
				vec->x = v;
				break;
			case 1:
				vec->y = v;
				break;
			case 2:
				vec->z = v;
				break;
			}
		}
	}
	return true;
}

static bool parse_color(Vector3 *col)
{
	for(int i=0; i<3; i++) {
		char *tok;

		if(!(tok = strtok(0, SEP)) || !is_float(tok)) {
			col->y = col->z = col->x;
			return i > 0 ? true : false;
		}

		float v = atof(tok);
		switch(i) {
		case 0:
			col->x = v;
			break;
		case 1:
			col->y = v;
			break;
		case 2:
			col->z = v;
			break;
		}
	}
	return true;
}

static bool parse_face(obj_face *face)
{
	char *tok[] = {0, 0, 0, 0};
	face->elem = 0;

	for(int i=0; i<4; i++) {
		if((!(tok[i] = strtok(0, SEP)) || !is_int(tok[i]))) {
			if(i < 3) return false;	// less than 3 verts? not a polygon
		} else {
			face->elem++;
		}
	}

	for(int i=0; i<4; i++) {
		char *subtok = tok[i];

		if(!subtok || !*subtok || !is_int(subtok)) {
			if(i < 3) {
				return false;
			}
			face->v[i] = INVALID_IDX;
		} else {
			face->v[i] = atoi(subtok);
			if(face->v[i] > 0) face->v[i]--;	/* convert to 0-based */
		}

		while(subtok && *subtok && *subtok != '/') {
			subtok++;
		}
		if(subtok && *subtok && *++subtok && is_int(subtok)) {
			face->t[i] = atoi(subtok);
			if(face->t[i] > 0) face->t[i]--;	/* convert to 0-based */
		} else {
			face->t[i] = INVALID_IDX;
		}

		while(subtok && *subtok && *subtok != '/') {
			subtok++;
		}
		if(subtok && *subtok && *++subtok && is_int(subtok)) {
			face->n[i] = atoi(subtok);
			if(face->n[i] > 0) face->n[i]--;	/* convert to 0-based */
		} else {
			face->n[i] = INVALID_IDX;
		}
	}

	return true;
}

static const char *parse_map()
{
	char *tok, *prev = 0;

	while((tok = strtok(0, SEP))) {
		prev = tok;
	}

	return prev ? prev : "";
}

static bool find_file(char *res, int sz, const char *fname, const char *path, const char *mode)
{
	FILE *fp;
	const char *beg, *end;
	int fnamelen = strlen(fname);

	beg = path;
	while(beg && *beg) {
		end = beg;
		while(*end && *end != ':') {
			end++;
		}

		int sz = end - beg + 1;
		char *pathname = (char*)alloca(sz + fnamelen + 2);
		memcpy(pathname, beg, sz);
		strcat(pathname, "/");
		strcat(pathname, fname);

		if((fp = fopen(pathname, mode))) {
			fclose(fp);
			strncpy(res, pathname, sz);
			return true;
		}

		beg += sz;
	}
	return false;
}

static const char *dirname(const char *str)
{
	static char buf[PATH_MAX];

	if(!str || !*str) {
		strcpy(buf, ".");
	} else {
		strncpy(buf, str, PATH_MAX);
		char *ptr = strrchr(buf, '/');

		if(*ptr) *ptr = 0;
	}
	return buf;
}
