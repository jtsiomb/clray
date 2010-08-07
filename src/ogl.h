#ifndef OGL_H_
#define OGL_H_

#ifndef __APPLE__

#if defined(WIN32) || defined(__WIN32__)
#include <windows.h>
#endif

#include <GL/gl.h>
#include <GL/glu.h>

#else	/* __APPLE__ */
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#endif


#endif	/* OGL_H_ */
