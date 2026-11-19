/*
 * gpushare.c - contexts that share objects, current in two threads at
 * once, as a threaded compositor uses them: EGL on the surfaceless platform, a
 * GLES 2 context per thread, the second sharing the first's objects.
 *
 *   gpushare [-o file] [-n rounds]
 *
 * Checks: the display (EGL_MESA_platform_surfaceless); two GLES 2 contexts,
 * the second sharing the first; a texture and a framebuffer made in one
 * thread seen in the other; each thread, run many times over the other
 * (the threads are switched pre-emptively, by PThreadTicker), always seeing
 * its own context's state; a fence made in one thread and waited for in
 * the other; what each thread's frame holds (on a GPU; with V3DM$Fake set
 * nothing is drawn, and that check is skipped). With RISC3D's command
 * stream (RISC OS on Linux) or in software, sharing gives an OSMesa
 * context or is refused: it says which.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

static FILE *out;
static int checks, failures;
static EGLDisplay dpy;
static EGLContext ca, cb;
static GLuint tex, fbo;
static volatile int b_ready, b_done, b_failed, a_round, b_round, a_started;
static int rounds = 2000, drawn;
static PFNEGLCREATESYNCKHRPROC create_sync;
static PFNEGLCLIENTWAITSYNCKHRPROC client_wait;
static PFNEGLDESTROYSYNCKHRPROC destroy_sync;
static EGLSyncKHR fence;
static volatile int fence_made;

#define CHECK(c, ...) do { checks++; if (c) { fprintf(out, "ok    "); } \
    else { failures++; fprintf(out, "FAIL  "); } fprintf(out, __VA_ARGS__); fputc('\n', out); fflush(out); } while (0)

/* the clear colour a thread set last: its context's own state */
static int clear_is(float r)
{
    GLfloat v[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, v);
    return v[0] > r - 0.01f && v[0] < r + 0.01f;
}

/* one pixel of the current framebuffer, as 0xAABBGGRR */
static unsigned pixel(void)
{
    unsigned char p[4] = { 0, 0, 0, 0 };
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned) p[3] << 24;
}

static void *thread_b(void *arg)
{
    int i, bad = 0;
    (void) arg;
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, cb)) {
        b_failed = 1;
        b_ready = 1;
        return NULL;
    }
    b_ready = 1;
    /* the first context's texture and framebuffer, seen from here */
    if (!glIsTexture(tex) || !glIsFramebuffer(fbo))
        b_failed |= 2;
    glClearColor(0.75f, 0, 0, 1);
    while (!a_started)                  /* both loops run at once */
        sched_yield();
    for (i = 0; i < rounds; i++) {
        if (!clear_is(0.75f))
            bad++;
        glClearColor(0.75f, 0, 0, 1);
        b_round = i;
        if (!(i & 63))
            sched_yield();
    }
    if (bad)
        b_failed |= 4;
    /* wait for the first thread's fence, then its texture is complete */
    while (!fence_made)
        sched_yield();
    if (client_wait && client_wait(dpy, fence, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR)
                       != EGL_CONDITION_SATISFIED_KHR)
        b_failed |= 8;
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    b_done = 1;
    return NULL;
}

int main(int argc, char **argv)
{
    static const EGLint cfg_attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    static const EGLint es2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
    EGLConfig cfg;
    EGLint n = 0;
    pthread_t tb;
    const char *ext, *renderer;
    int i, bad = 0, overlap = 0;

    out = stdout;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            FILE *f = fopen(argv[++i], "w");
            if (f)
                out = f;
        } else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            rounds = atoi(argv[++i]);
        }
    }
    fprintf(out, "gpushare: shared GLES 2 contexts in two threads\n");

    ext = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
    CHECK(ext && strstr(ext, "EGL_MESA_platform_surfaceless") && get_platform_display,
          "EGL_MESA_platform_surfaceless listed");
    dpy = get_platform_display ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL) : EGL_NO_DISPLAY;
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL), "the surfaceless platform's display");
    if (dpy == EGL_NO_DISPLAY)
        return 1;
    create_sync = (PFNEGLCREATESYNCKHRPROC) eglGetProcAddress("eglCreateSyncKHR");
    client_wait = (PFNEGLCLIENTWAITSYNCKHRPROC) eglGetProcAddress("eglClientWaitSyncKHR");
    destroy_sync = (PFNEGLDESTROYSYNCKHRPROC) eglGetProcAddress("eglDestroySyncKHR");
    eglBindAPI(EGL_OPENGL_ES_API);
    CHECK(eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &n) && n == 1, "a GLES 2 config");

    ca = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, es2);
    CHECK(ca != EGL_NO_CONTEXT, "context A");
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ca), "A current, no surface");
    renderer = (const char *) glGetString(GL_RENDERER);
    fprintf(out, "      renderer: %s\n", renderer ? renderer : "(none)");
    fprintf(out, "      EGL_VERSION: %s\n", eglQueryString(dpy, EGL_VERSION));

    /* a texture A draws into, through a framebuffer object */
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "A: a framebuffer on a texture");

    cb = eglCreateContext(dpy, cfg, ca, es2);
    if (cb == EGL_NO_CONTEXT) {
        fprintf(out, "      sharing refused (EGL error %x): no shared contexts here "
                "(RISC3D's command stream, or software sharing only with software)\n", eglGetError());
        fprintf(out, "%d checks, %d failures: %s\n", checks, failures, failures ? "FAILED" : "ALL PASS (no sharing)");
        return failures != 0;
    }
    CHECK(1, "context B, sharing A's objects");

    glClearColor(0.25f, 0, 0, 1);
    if (pthread_create(&tb, NULL, thread_b, NULL)) {
        CHECK(0, "a second thread");
        return 1;
    }
    while (!b_ready)
        sched_yield();
    CHECK(!(b_failed & 1), "B current in the second thread while A is current here");
    a_started = 1;
    for (i = 0; i < rounds; i++) {
        if (!clear_is(0.25f))
            bad++;
        if (b_round > 0 && b_round < rounds - 1)
            overlap++;
        glClearColor(0.25f, 0, 0, 1);
        a_round = i;
        if (!(i & 63))
            sched_yield();
    }
    CHECK(!bad, "this thread always saw A's state (%d of %d wrong; B was at round %d)", bad, rounds, b_round);
    CHECK(overlap > 0, "the two threads ran at the same time (B mid-way during %d of A's %d rounds)", overlap, rounds);

    /* draw in A, fence, and let B wait for it */
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    drawn = pixel() == 0xFF00FF00u;
    fence = create_sync ? create_sync(dpy, EGL_SYNC_FENCE_KHR, NULL) : EGL_NO_SYNC_KHR;
    CHECK(fence != EGL_NO_SYNC_KHR, "a fence in A");
    fence_made = 1;
    pthread_join(tb, NULL);
    CHECK(b_done && !(b_failed & 2), "B sees A's texture and framebuffer");
    CHECK(b_done && !(b_failed & 4), "B always saw its own state while both ran");
    CHECK(b_done && !(b_failed & 8), "B waited for A's fence");
    if (getenv("V3DM$Fake"))
        fprintf(out, "      (V3DM$Fake: nothing drawn, the picture isn't checked)\n");
    else
        CHECK(drawn, "A's framebuffer holds what A drew");
    if (destroy_sync && fence != EGL_NO_SYNC_KHR)
        destroy_sync(dpy, fence);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, cb);
    eglDestroyContext(dpy, ca);
    eglTerminate(dpy);
    fprintf(out, "%d checks, %d failures: %s\n", checks, failures, failures ? "FAILED" : "ALL PASS");
    return failures != 0;
}
