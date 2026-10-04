/*
 * render-paths.c - riscos-mesa rendering check: the fast paths stay off
 * in states they don't handle (found by the 2026-10-04 audit).
 *
 *   - OSMesa's own depth-tested triangles and lines write to the window's
 *     rows, so they must not be used while a framebuffer object is bound
 *     (they drew into the window instead, and crashed with an FBO larger
 *     than the window): an FBO with 16- and 24-bit depth, smaller and
 *     larger than the window, smooth and flat; the FBO must get the
 *     drawing and the window none.
 *   - nor with GL_DEPTH_CLAMP (they don't clamp depth): the pixel must
 *     match the general path (an always-passing alpha test forces it).
 *   - the fast textured triangles under GL_FASTEST: GL_CLAMP with a
 *     GL_LINEAR filter must blend in the border colour, as the general
 *     path does (a second, white texture unit forces it).
 * One line per case; run.sh compares them with expected/render-paths.txt.
 * Part of riscos-mesa, MIT licence.
 */
#define GL_GLEXT_PROTOTYPES
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define W 64
#define H 64
static unsigned char win[W * H * 4];

static void fbo_case(int ctxdepth, int fbodepth, int size, int flat, int lines)
{
    static unsigned char fb[256 * 256 * 4];
    OSMesaContext c = OSMesaCreateContextExt(OSMESA_RGBA, ctxdepth, 0, 0, NULL);
    GLuint f, cr, dr;
    int i, in_fbo = 0, in_win = 0;
    OSMesaMakeCurrent(c, win, GL_UNSIGNED_BYTE, W, H);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glGenRenderbuffers(1, &cr);
    glBindRenderbuffer(GL_RENDERBUFFER, cr);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, size, size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, cr);
    glGenRenderbuffers(1, &dr);
    glBindRenderbuffer(GL_RENDERBUFFER, dr);
    glRenderbufferStorage(GL_RENDERBUFFER, fbodepth == 16 ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT24,
                          size, size);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, dr);
    glViewport(0, 0, size, size);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glShadeModel(flat ? GL_FLAT : GL_SMOOTH);
    glColor3f(1, 0, 0);
    if (lines) {
        glBegin(GL_LINES);
        for (i = 0; i < 8; i++) { glVertex2f(-0.9f, -0.9f + 0.2f * i); glVertex2f(0.9f, -0.9f + 0.2f * i); }
        glEnd();
    } else {
        glBegin(GL_TRIANGLES);
        glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1);
        glEnd();
    }
    glFinish();
    glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, fb);
    for (i = 0; i < size * size; i++) if (fb[i * 4] == 255) in_fbo++;
    for (i = 0; i < W * H; i++) if (win[i * 4] == 255) in_win++;
    printf("fbo: context depth %d, fbo %dx%d depth %d, %s %s: ",
           ctxdepth, size, size, fbodepth, flat ? "flat" : "smooth", lines ? "lines" : "triangle");
    if (in_fbo > 0 && in_win == 0) printf("ok\n");
    else printf("FAIL (%d pixels drawn in the fbo, %d in the window)\n", in_fbo, in_win);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    OSMesaDestroyContext(c);
}

/* three overlapping triangles, one in range and two beyond the near and
   far planes, which depth clamping keeps and orders by clamped depth */
static void clamp_draw(int general)
{
    glClearColor(0, 0, 0, 1);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_CLAMP);
    if (general) { glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_ALWAYS, 0); }
    else glDisable(GL_ALPHA_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex3f(-1, -1, 0.5f); glVertex3f(3, -1, 0.5f); glVertex3f(-1, 3, 0.5f);
    glColor3f(0, 1, 0); glVertex3f(-1, -1, 3); glVertex3f(3, -1, 3); glVertex3f(-1, 3, 3);
    glColor3f(0, 0, 1); glVertex3f(-1, -1, -3); glVertex3f(3, -1, -3); glVertex3f(-1, 3, -3);
    glEnd();
    glFinish();
}

static void clamp_case(int depth)
{
    unsigned char fast[4];
    OSMesaContext c = OSMesaCreateContextExt(OSMESA_RGBA, depth, 0, 0, NULL);
    OSMesaMakeCurrent(c, win, GL_UNSIGNED_BYTE, W, H);
    glViewport(0, 0, W, H);
    clamp_draw(0);
    memcpy(fast, win + (32 * W + 32) * 4, 4);
    clamp_draw(1);
    printf("depth clamp, depth %d: ", depth);
    if (!memcmp(fast, win + (32 * W + 32) * 4, 3)) printf("ok\n");
    else printf("FAIL (fast %d,%d,%d, general %d,%d,%d)\n", fast[0], fast[1], fast[2],
                win[(32 * W + 32) * 4], win[(32 * W + 32) * 4 + 1], win[(32 * W + 32) * 4 + 2]);
    OSMesaDestroyContext(c);
}

static GLuint tex, white;
static void tex_draw(int general, GLenum wrap, GLenum minf, GLenum magf)
{
    static const float red[4] = {1, 0, 0, 1};
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, white);
    if (general) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEnable(GL_TEXTURE_2D);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magf);
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, red);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glMultiTexCoord2f(GL_TEXTURE0, -0.25f, -0.25f); glMultiTexCoord2f(GL_TEXTURE1, 0, 0); glVertex2f(-1, -1);
    glMultiTexCoord2f(GL_TEXTURE0, 1.25f, -0.25f); glVertex2f(1, -1);
    glMultiTexCoord2f(GL_TEXTURE0, 1.25f, 1.25f); glVertex2f(1, 1);
    glMultiTexCoord2f(GL_TEXTURE0, -0.25f, 1.25f); glVertex2f(-1, 1);
    glEnd();
    glFinish();
}

static void tex_case(const char *name, GLenum ifmt, GLenum wrap, GLenum minf, GLenum magf)
{
    static unsigned char fast[W * H * 4];
    int i, md = 0;
    unsigned char img[8 * 8 * 4];
    for (i = 0; i < 64; i++) {
        img[i * 4] = (unsigned char)((i % 8) * 36); img[i * 4 + 1] = (unsigned char)((i / 8) * 36);
        img[i * 4 + 2] = 128; img[i * 4 + 3] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    tex_draw(0, wrap, minf, magf);
    memcpy(fast, win, sizeof fast);
    tex_draw(1, wrap, minf, magf);
    for (i = 0; i < W * H * 4; i++) { int d = abs(fast[i] - win[i]); if (d > md) md = d; }
    /* the fast path rounds by up to 2/255 differently (patches/mesa/README) */
    if (md <= 2) printf("texture, GL_FASTEST, %s: ok\n", name);
    else printf("texture, GL_FASTEST, %s: FAIL (differs from the general path by %d)\n", name, md);
}

int main(void)
{
    static const unsigned char w4[4] = {255, 255, 255, 255};
    OSMesaContext c;
    setvbuf(stdout, NULL, _IONBF, 0);     /* the lines so far, if a case crashes */
    fbo_case(24, 24, 64, 0, 0);
    fbo_case(24, 24, 64, 1, 0);
    fbo_case(24, 16, 64, 0, 0);
    fbo_case(16, 16, 64, 0, 0);
    fbo_case(16, 16, 64, 1, 1);
    fbo_case(24, 24, 256, 0, 0);
    clamp_case(16);
    clamp_case(24);
    c = OSMesaCreateContextExt(OSMESA_RGBA, 0, 0, 0, NULL);
    OSMesaMakeCurrent(c, win, GL_UNSIGNED_BYTE, W, H);
    glViewport(0, 0, W, H);
    glGenTextures(1, &white);
    glBindTexture(GL_TEXTURE_2D, white);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, w4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glGenTextures(1, &tex);
    tex_case("RGB GL_CLAMP min NEAREST mag LINEAR", GL_RGB, GL_CLAMP, GL_NEAREST, GL_LINEAR);
    tex_case("RGBA GL_CLAMP min NEAREST mag LINEAR", GL_RGBA, GL_CLAMP, GL_NEAREST, GL_LINEAR);
    tex_case("RGBA GL_CLAMP min LINEAR mag NEAREST", GL_RGBA, GL_CLAMP, GL_LINEAR, GL_NEAREST);
    tex_case("RGBA GL_CLAMP NEAREST", GL_RGBA, GL_CLAMP, GL_NEAREST, GL_NEAREST);
    tex_case("RGB GL_CLAMP_TO_EDGE min NEAREST mag LINEAR", GL_RGB, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_LINEAR);
    OSMesaDestroyContext(c);
    return 0;
}
