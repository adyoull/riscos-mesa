/*
 * fg_gles_riscos.c
 *
 * The RISC OS back end of freeglut, OpenGL ES build (-DFREEGLUT_GLES):
 * GLUT's fonts draw with OpenGL 1.x calls ES doesn't have, so they do
 * nothing here (as in freeglut's own gles_stubs.c). Menus are Wimp menus,
 * so unlike other ES builds of freeglut, the menu calls work: this file
 * includes fg_menu.c for them.
 *
 * Copyright (c) 2026 Andrew Youll. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <GL/freeglut.h>
#include "../fg_internal.h"

#ifdef FREEGLUT_GLES

/* freeglut's menu code (fg_menu.c) keeps the menu trees that the Wimp
   menus are built from. Its OpenGL drawing of menus is never used here
   (and needs OpenGL 1.x), so compile it with those calls turned off. */
#define glBegin(m)              ((void) 0)
#define glEnd()                 ((void) 0)
#define glVertex2i(x, y)        ((void) 0)
#define glColor4f(r, g, b, a)   ((void) 0)
#define glColor4fv(v)           ((void) 0)
#define glRasterPos2i(x, y)     ((void) 0)
#define glPushAttrib(m)         ((void) 0)
#define glPopAttrib()           ((void) 0)
#define glPushMatrix()          ((void) 0)
#define glPopMatrix()           ((void) 0)
#define glMatrixMode(m)         ((void) 0)
#define glLoadIdentity()        ((void) 0)
#define glOrtho(l, r, b, t, n, f) ((void) 0)
#define glDisable(c)            ((void) 0)
#define GL_QUADS         0
#define GL_QUAD_STRIP    0
#define GL_TEXTURE_BIT   0
#define GL_LIGHTING_BIT  0
#define GL_POLYGON_BIT   0
#ifndef GL_LIGHTING                     /* not in OpenGL ES 2.0 */
#define GL_LIGHTING      0
#define GL_MODELVIEW     0
#define GL_PROJECTION    0
#endif
#include "../fg_menu.c"

SFG_Font* fghFontByID( void* font ) { (void) font; return NULL; }

void    FGAPIENTRY glutBitmapCharacter( void* font, int character ) { (void) font; (void) character; }
int     FGAPIENTRY glutBitmapWidth( void* font, int character ) { (void) font; (void) character; return 0; }
void    FGAPIENTRY glutStrokeCharacter( void* font, int character ) { (void) font; (void) character; }
int     FGAPIENTRY glutStrokeWidth( void* font, int character ) { (void) font; (void) character; return 0; }
GLfloat FGAPIENTRY glutStrokeWidthf( void* font, int character ) { (void) font; (void) character; return 0.0f; }
int     FGAPIENTRY glutBitmapLength( void* font, const unsigned char* string ) { (void) font; (void) string; return 0; }
int     FGAPIENTRY glutStrokeLength( void* font, const unsigned char* string ) { (void) font; (void) string; return 0; }
GLfloat FGAPIENTRY glutStrokeLengthf( void* font, const unsigned char *string ) { (void) font; (void) string; return 0.0f; }
int     FGAPIENTRY glutBitmapHeight( void* font ) { (void) font; return 0; }
GLfloat FGAPIENTRY glutStrokeHeight( void* font ) { (void) font; return 0.0f; }
void    FGAPIENTRY glutBitmapString( void* font, const unsigned char *string ) { (void) font; (void) string; }
void    FGAPIENTRY glutStrokeString( void* font, const unsigned char *string ) { (void) font; (void) string; }

#endif
