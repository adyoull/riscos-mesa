/*
 * render-stencil2.c - riscos-mesa rendering check (host copy of
 * tests/stenciltest.c, on OSMesa): two-sided stencil through each API a program may use
 * (GL_EXT_stencil_two_side, GL_ATI_separate_stencil, GL 2.0's
 * glStencilOpSeparate), z-pass, as a shadow volume does it: front faces
 * increment, back faces decrement. Unlike a red/green check, each column's
 * own value is checked (1, 0, 255), so swapped faces show as well as a
 * face that wasn't set. Functions come from eglGetProcAddress, as SDL's
 * programs get them (David Sharp's d3gltest found the EXT one failing on
 * the GPU: glActiveStencilFaceEXT wasn't the GPU library's).
 *
 *   stenciltest [-o file]
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

#ifndef GL_STENCIL_TEST_TWO_SIDE_EXT
#define GL_STENCIL_TEST_TWO_SIDE_EXT 0x8910
#endif
#ifndef GL_INCR_WRAP
#define GL_INCR_WRAP 0x8507
#define GL_DECR_WRAP 0x8508
#endif

#define W 192
#define H 64
static FILE *out;
static int fails;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out ? out : stdout, fmt, ap);
    va_end(ap);
    if (out) fflush(out);
}

static void quad(float x0, float x1, int cw)
{
    glBegin(GL_QUADS);
    if (!cw) { glVertex2f(x0, 0); glVertex2f(x1, 0); glVertex2f(x1, H); glVertex2f(x0, H); }
    else     { glVertex2f(x0, 0); glVertex2f(x0, H); glVertex2f(x1, H); glVertex2f(x1, 0); }
    glEnd();
}

/* paint where the stencil is v in colour c (&BBGGRR) */
static void paint(int v, float r, float g, float b)
{
    glStencilFunc(GL_EQUAL, v, 0xff);
    glColor3f(r, g, b);
    quad(0, W, 0);
}

static void check(const char *name)
{
    static const unsigned want[3] = { 0x0000FF, 0x00FF00, 0xFF0000 };  /* 1 red, 0 green, 255 blue */
    static const char *const what[3] = { "front only: 1 (red)", "both: 0 (green)", "back only: 255 (blue)" };
    unsigned char c[4];
    int i, ok = 1;
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    paint(1, 1, 0, 0);
    paint(0, 0, 1, 0);
    paint(255, 0, 0, 1);
    glFinish();
    for (i = 0; i < 3; i++) {
        unsigned got;
        glReadPixels(W / 6 + i * W / 3, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
        got = c[0] | c[1] << 8 | (unsigned) c[2] << 16;
        if (got != want[i]) {
            say("FAIL %s: column %d (%s) is &%06X\n", name, i, what[i], got);
            ok = 0;
        }
    }
    if (ok) say("PASS %s\n", name);
    else fails++;
}

static void begin(void)
{
    glColorMask(1, 1, 1, 1);
    glClearColor(0, 0, 0, 1);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 0, 0xff);
    glStencilMask(0xff);
    glColorMask(0, 0, 0, 0);
}

int main(int argc, char **argv)
{
    void (*active_face)(GLenum);
    void (*op_ati)(GLenum, GLenum, GLenum, GLenum);
    void (*op_sep)(GLenum, GLenum, GLenum, GLenum);
    OSMesaContext c = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
    static unsigned char buf[W * H * 4];
    GLint bits = 0;
    OSMesaMakeCurrent(c, buf, GL_UNSIGNED_BYTE, W, H);
    glGetIntegerv(GL_STENCIL_BITS, &bits);
    say("%d stencil bits\n", bits);
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    active_face = (void (*)(GLenum)) OSMesaGetProcAddress("glActiveStencilFaceEXT");
    op_ati = (void (*)(GLenum, GLenum, GLenum, GLenum)) OSMesaGetProcAddress("glStencilOpSeparateATI");
    op_sep = (void (*)(GLenum, GLenum, GLenum, GLenum)) OSMesaGetProcAddress("glStencilOpSeparate");
    /* GL_EXT_stencil_two_side, as d3gltest and Doom 3 do it */
    if (active_face) {
        begin();
        glEnable(GL_STENCIL_TEST_TWO_SIDE_EXT);
        active_face(GL_BACK);
        glStencilOp(GL_KEEP, GL_KEEP, GL_DECR_WRAP);
        active_face(GL_FRONT);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INCR_WRAP);
        quad(0, 2 * W / 3, 0);          /* front, over the left two thirds */
        quad(W / 3, W, 1);              /* back, over the right two thirds */
        glDisable(GL_STENCIL_TEST_TWO_SIDE_EXT);
        glColorMask(1, 1, 1, 1);
        check("EXT_stencil_two_side");
    } else {
        say("SKIP EXT_stencil_two_side: no glActiveStencilFaceEXT\n");
    }
    if (op_ati) {
        begin();
        op_ati(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
        op_ati(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
        quad(0, 2 * W / 3, 0);
        quad(W / 3, W, 1);
        glColorMask(1, 1, 1, 1);
        check("ATI_separate_stencil");
    } else {
        say("SKIP ATI_separate_stencil: no glStencilOpSeparateATI\n");
    }
    if (op_sep) {
        begin();
        op_sep(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
        op_sep(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
        quad(0, 2 * W / 3, 0);
        quad(W / 3, W, 1);
        glColorMask(1, 1, 1, 1);
        check("glStencilOpSeparate (GL 2.0)");
    }
    say("%d failures\n", fails);
    return 0;
}
