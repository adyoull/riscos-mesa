/*
 * r3d_e2e.c - riscos-mesa's EGL with RISC3D's real client library and
 * Linux helper, end to end on the build machine.
 *
 * The program calls EGL and GL as a RISC OS program would (against the
 * fake RISC OS in fake_riscos.c); EGL gives the context to RISC3D, whose
 * client (built with its host stand-in for the SWIs) sends the calls to
 * its helper (rol-3d, run on this machine), which draws them with this
 * machine's Mesa. The same scene is then drawn by OSMesa (RISC3D$Disable)
 * and the two pictures on the fake screen are compared.
 *
 * Built and run by tests/run-all.sh (step "risc3d", with R3D_SRC set to a
 * RISC3D source tree); see tests/host-harness/egl/README.md.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_riscos.h>
#include <GL/gl.h>
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "fake_riscos.h"

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define W 100
#define H 80
static EGLDisplay dpy;

static EGLConfig choose(EGLint visual, EGLint depth, EGLint type)
{
    EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, type, EGL_DEPTH_SIZE, depth, EGL_NONE };
    EGLConfig c[16];
    EGLint n = 0, i, v;
    eglChooseConfig(dpy, a, c, 16, &n);
    for (i = 0; i < n; i++) {
        eglGetConfigAttrib(dpy, c[i], EGL_NATIVE_VISUAL_ID, &v);
        if (v == visual)
            return c[i];
    }
    return NULL;
}

/* A fixed-function scene: blue background, a red triangle over the bottom
   left half, a smooth-shaded quad in front of it (depth tested), and a
   lit, rotated quad: immediate mode, matrices, lighting, depth. */
static void scene(void)
{
    static const GLfloat light[] = { 0.3f, 0.5f, 1.0f, 0.0f };
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_DEPTH_TEST);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0);
    glVertex3f(-1, -1, 0.5f); glVertex3f(1, -1, 0.5f); glVertex3f(-1, 1, 0.5f);
    glEnd();
    glShadeModel(GL_SMOOTH);
    glBegin(GL_QUADS);
    glColor3f(0, 1, 0); glVertex3f(-0.5f, -0.5f, 0.8f);
    glColor3f(1, 1, 0); glVertex3f(0.0f, -0.5f, 0.8f);
    glColor3f(1, 1, 1); glVertex3f(0.0f, 0.0f, 0.8f);
    glColor3f(0, 1, 1); glVertex3f(-0.5f, 0.0f, 0.8f);
    glEnd();
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightfv(GL_LIGHT0, GL_POSITION, light);
    glPushMatrix();
    glTranslatef(0.5f, 0.5f, -0.2f);
    glRotatef(30, 0, 0, 1);
    glNormal3f(0, 0, 1);
    glBegin(GL_QUADS);
    glVertex3f(-0.25f, -0.25f, 0); glVertex3f(0.25f, -0.25f, 0);
    glVertex3f(0.25f, 0.25f, 0); glVertex3f(-0.25f, 0.25f, 0);
    glEnd();
    glPopMatrix();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
}

/* the window's pixels on the fake screen: (100, 250) for 100 x 80 */
static void grab(unsigned int *out)
{
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            out[y * W + x] = fake_screen_pixel(100 + x, 250 + y) & 0xFFFFFF;
}

static int differ(unsigned int a, unsigned int b, int tol)
{
    int i;
    for (i = 0; i < 24; i += 8) {
        int d = (int) ((a >> i) & 255) - (int) ((b >> i) & 255);
        if (d > tol || d < -tol)
            return 1;
    }
    return 0;
}

static void *run(void *arg)
{
    static unsigned int a[W * H], b[W * H];
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface ws;
    const char *r;
    int i, n;
    (void) arg;

    fake_set_screen(640, 480, 0, 5);
    fake_reset_clip();
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    CHECK(eglInitialize(dpy, NULL, NULL), "initialise");
    eglBindAPI(EGL_OPENGL_API);
    cfg = choose(EGL_RISCOS_VISUAL_TBGR, 16, EGL_WINDOW_BIT);
    fake_open_window(0x1000, 200, 300, 400, 460, 0, 0);
    ws = eglCreateWindowSurface(dpy, cfg, 0x1000, NULL);
    CHECK(ws != EGL_NO_SURFACE, "window surface");

    /* RISC3D */
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, ws, ws, ctx), "context current");
    r = (const char *) glGetString(GL_RENDERER);
    CHECK(r && strstr(r, "RISC3D"), "drawn by RISC3D: %s", r ? r : "(none)");
    r = (const char *) glGetString(GL_VERSION);
    CHECK(r && (!strncmp(r, "2.1 RISC3D", 10) || !strncmp(r, "3.0 RISC3D", 10) || !strncmp(r, "3.1 RISC3D", 10)),
          "GL_VERSION what the stream carries (2.1 to 3.1): %s", r ? r : "(none)");
    scene();
    CHECK(eglSwapBuffers(dpy, ws), "swap");
    grab(a);
    {
        GLfloat m[16];
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(1, 2, 3);
        glGetFloatv(GL_MODELVIEW_MATRIX, m);
        CHECK(m[12] == 1 && m[13] == 2 && m[14] == 3, "glGetFloatv(GL_MODELVIEW_MATRIX) answered");
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);

    /* OSMesa, for comparison */
    setenv("RISC3D$Disable", "1", 1);
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, ws, ws, ctx), "OSMesa context current");
    r = (const char *) glGetString(GL_RENDERER);
    CHECK(r && strstr(r, "OffScreen"), "drawn by OSMesa: %s", r ? r : "(none)");
    scene();
    eglSwapBuffers(dpy, ws);
    grab(b);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    unsetenv("RISC3D$Disable");

    /* The two pictures: the same but for rasterisation details at edges */
    for (n = 0, i = 0; i < W * H; i++)
        n += differ(a[i], b[i], 6);
    CHECK(n * 100 <= W * H * 2, "RISC3D's picture matches OSMesa's (%d of %d pixels differ)", n, W * H);
    CHECK((a[5 * W + 90] & 0xFFFFFF) == 0xFF0000, "background blue (%06x)", a[5 * W + 90]);
    CHECK((a[75 * W + 5] & 0xFFFFFF) == 0x0000FF, "red triangle bottom left (%06x)", a[75 * W + 5]);
    {
        FILE *f = fopen("r3d_e2e.ppm", "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", W * 2 + 4, H);
            for (i = 0; i < H; i++) {
                int x;
                for (x = 0; x < W * 2 + 4; x++) {
                    unsigned int p = x < W ? a[i * W + x] : x < W + 4 ? 0x808080 : b[i * W + x - W - 4];
                    fputc(p & 255, f); fputc((p >> 8) & 255, f); fputc((p >> 16) & 255, f);
                }
            }
            fclose(f);
        }
    }

    /* Overlapped swaps (EGL$Overlapped): each swap shows the frame before */
    setenv("EGL$Overlapped", "1", 1);
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    unsetenv("EGL$Overlapped");
    CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, ws, ws, ctx), "overlapped context current");
    glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); eglSwapBuffers(dpy, ws);
    glClearColor(0, 1, 0, 1); glClear(GL_COLOR_BUFFER_BIT); eglSwapBuffers(dpy, ws);
    CHECK((fake_screen_pixel(150, 290) & 0xFFFFFF) == 0x0000FF,
          "overlapped: the second swap shows the first frame (%06x)", fake_screen_pixel(150, 290) & 0xFFFFFF);
    glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT); eglSwapBuffers(dpy, ws);
    CHECK((fake_screen_pixel(150, 290) & 0xFFFFFF) == 0x00FF00,
          "overlapped: the third shows the second (%06x)", fake_screen_pixel(150, 290) & 0xFFFFFF);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);

    /* OpenGL ES 1.1 (RISC3D 0.4): drawn by RISC3D too, with vertex arrays */
    {
        static const GLfloat tri[] = { -1, -1, 1, -1, -1, 1 };
        EGLint es1[] = { EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE };
        unsigned int c[W * H];
        eglBindAPI(EGL_OPENGL_ES_API);
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, es1);
        CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, ws, ws, ctx), "GLES 1.1 context current");
        r = (const char *) glGetString(GL_VERSION);
        CHECK(r && !strncmp(r, "OpenGL ES-CM 1.1 RISC3D", 23), "GLES 1.1 drawn by RISC3D: %s", r ? r : "(none)");
        glViewport(0, 0, W, H);
        glClearColor(0, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glColor4f(1, 0, 0, 1);
        glVertexPointer(2, GL_FLOAT, 0, tri);
        glEnableClientState(GL_VERTEX_ARRAY);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisableClientState(GL_VERTEX_ARRAY);
        CHECK(eglSwapBuffers(dpy, ws), "GLES 1.1 swap");
        grab(c);
        CHECK(c[75 * W + 5] == 0x0000FF && c[5 * W + 90] == 0xFF0000,
              "GLES 1.1: red triangle bottom left (%06x), blue top right (%06x)", c[75 * W + 5], c[5 * W + 90]);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(dpy, ctx);
        eglBindAPI(EGL_OPENGL_API);
    }

    /* OpenGL ES 3.0 and OpenGL 3.1 (RISC3D 0.5): asked for by version */
    {
        EGLint es30[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 0, EGL_NONE };
        EGLint gl31[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 1, EGL_NONE };
        EGLint rt = 0;
        CHECK(eglGetConfigAttrib(dpy, cfg, EGL_RENDERABLE_TYPE, &rt) && (rt & 0x40), "configs offer GLES 3 (%x)", rt);
        eglBindAPI(EGL_OPENGL_ES_API);
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, es30);
        CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, ws, ws, ctx), "GLES 3.0 context current");
        r = (const char *) glGetString(GL_VERSION);
        CHECK(r && !strncmp(r, "OpenGL ES 3.", 12) && strstr(r, "RISC3D"), "GLES 3 drawn by RISC3D: %s", r ? r : "(none)");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(dpy, ctx);
        eglBindAPI(EGL_OPENGL_API);
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, gl31);
        CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, ws, ws, ctx), "GL 3.1 context current");
        r = (const char *) glGetString(GL_VERSION);
        CHECK(r && !strncmp(r, "3.1 RISC3D", 10), "GL 3.1 drawn by RISC3D: %s", r ? r : "(none)");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(dpy, ctx);
    }

    /* A pixmap: glFinish brings the sprite up to date */
    {
        int *area = malloc(16 + 44 + 16 * 8 * 4), *spr = area + 4;
        unsigned int *px = (unsigned int *) ((char *) spr + 44);
        EGLConfig pc = choose(EGL_RISCOS_VISUAL_TBGR, 0, EGL_PIXMAP_BIT);
        EGLSurface ps;
        area[0] = 16 + 44 + 16 * 8 * 4; area[1] = 1; area[2] = 16; area[3] = area[0];
        memset(spr, 0, 44 + 16 * 8 * 4);
        spr[0] = 44 + 16 * 8 * 4;
        spr[4] = 15; spr[5] = 7; spr[7] = 31; spr[8] = 44; spr[9] = 44;
        spr[10] = 1 | (90 << 1) | (90 << 14) | (6 << 27);
        ps = eglCreatePixmapSurface(dpy, pc, spr, NULL);
        ctx = eglCreateContext(dpy, pc, EGL_NO_CONTEXT, NULL);
        CHECK(ps != EGL_NO_SURFACE && eglMakeCurrent(dpy, ps, ps, ctx), "RISC3D on a pixmap");
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        CHECK((px[0] & 0xFFFFFF) == 0x00FF00 && (px[16 * 8 - 1] & 0xFFFFFF) == 0x00FF00,
              "glFinish: the frame in the sprite (%06x)", px[0] & 0xFFFFFF);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(dpy, ctx);
        eglDestroySurface(dpy, ps);
        free(area);
    }
    eglTerminate(dpy);
    printf("%d checks, %d failures: %s\n", checks, failures, failures ? "FAIL" : "ALL PASS");
    return NULL;
}

int main(void)
{
    /* As harness.c: allocations and the stack below 2 GB (32-bit SWI registers) */
    pthread_attr_t attr;
    pthread_t t;
    size_t stack_size = 8 << 20;
    void *stack;
    mallopt(M_ARENA_MAX, 1);
    mallopt(M_MMAP_MAX, 0);
    mallopt(M_TOP_PAD, 64 << 20);
    stack = mmap((void *) 0x60000000, stack_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (stack == MAP_FAILED) { perror("mmap"); return 2; }
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, stack_size);
    pthread_create(&t, &attr, run, NULL);
    pthread_join(t, NULL);
    return failures ? 1 : 0;
}
