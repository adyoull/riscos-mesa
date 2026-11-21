/*
 * gpuimage.c - the GPU drawing straight into the program's own memory
 * (EGL_RISCOS_image_memory), as a compositor would use it: an
 * EGLImage over a page-aligned buffer, made a renderbuffer and a
 * framebuffer's colour attachment, drawn, finished, and read by the
 * processor with no glReadPixels.
 *
 *   gpuimage [-o file] [-n frames]
 *
 * Checks: the extension listed (where it isn't, nothing else is tried:
 * software, RISC OS on Linux, an older GPU library); bad parameters
 * refused; an image in B,G,R,A bytes (cairo's ARGB32) and one in R,G,B,A;
 * a framebuffer on each; what the GPU drew in the memory, in that byte
 * order, row 0 GL's bottom row; glReadPixels agreeing; the image as a
 * texture drawn into the other; a fence; eglDestroyImageKHR. Then the time
 * for a 1024x768 frame drawn into memory against drawn and read back with
 * glReadPixels. With V3DM$Fake set nothing is drawn: the pictures aren't
 * checked.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_riscos.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#define FOURCC_AR24 0x34325241          /* DRM_FORMAT_ARGB8888: B,G,R,A bytes */
#define FOURCC_AB24 0x34324241          /* DRM_FORMAT_ABGR8888: R,G,B,A bytes */

static FILE *out;
static int checks, failures, drawn = 1;
static EGLDisplay dpy;
static PFNEGLCREATEIMAGEKHRPROC create_image;
static PFNEGLDESTROYIMAGEKHRPROC destroy_image;
static PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC image_storage;
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_texture;

#define CHECK(c, ...) do { checks++; if (c) { fprintf(out, "ok    "); } \
    else { failures++; fprintf(out, "FAIL  "); } fprintf(out, __VA_ARGS__); fputc('\n', out); fflush(out); } while (0)

static double now(void)
{
    return (double) clock() / CLOCKS_PER_SEC;
}

/* page-aligned memory, whole pages */
static unsigned char *pages(size_t bytes)
{
    unsigned char *p = (unsigned char *) memalign(4096, (bytes + 4095) & ~(size_t) 4095);
    if (p)
        memset(p, 0x11, (bytes + 4095) & ~(size_t) 4095);
    return p;
}

static EGLImageKHR image(void *mem, int w, int h, int row_bytes, int fourcc)
{
    EGLint a[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_ROW_BYTES_RISCOS, row_bytes,
                   EGL_LINUX_DRM_FOURCC_EXT, fourcc, EGL_NONE };
    return create_image(dpy, EGL_NO_CONTEXT, EGL_MEMORY_RISCOS, (EGLClientBuffer) mem, a);
}

/* a framebuffer whose colour buffer is the image */
static GLuint framebuffer(EGLImageKHR img, GLuint *rb)
{
    GLuint fb;
    glGenRenderbuffers(1, rb);
    glBindRenderbuffer(GL_RENDERBUFFER, *rb);
    image_storage(GL_RENDERBUFFER, (GLeglImageOES) img);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, *rb);
    return fb;
}

/* the 4 bytes at row y of memory with rows row_bytes apart, as b0 b1 b2 b3 */
static unsigned at(const unsigned char *mem, int row_bytes, int x, int y)
{
    const unsigned char *p = mem + (size_t) y * row_bytes + x * 4;
    return (unsigned) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static GLuint program(void)
{
    static const char *vs = "attribute vec2 p; varying vec2 t;\n"
                            "void main() { t = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n";
    static const char *fs = "precision mediump float; varying vec2 t; uniform sampler2D s;\n"
                            "void main() { gl_FragColor = texture2D(s, t); }\n";
    GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER), p = glCreateProgram();
    glShaderSource(v, 1, &vs, NULL);
    glCompileShader(v);
    glShaderSource(f, 1, &fs, NULL);
    glCompileShader(f);
    glAttachShader(p, v);
    glAttachShader(p, f);
    glBindAttribLocation(p, 0, "p");
    glLinkProgram(p);
    return p;
}

int main(int argc, char **argv)
{
    static const EGLint cfg_attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    static const EGLint es2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
    PFNEGLCREATESYNCKHRPROC create_sync;
    PFNEGLCLIENTWAITSYNCKHRPROC client_wait;
    PFNEGLDESTROYSYNCKHRPROC destroy_sync;
    EGLConfig cfg;
    EGLContext ctx;
    EGLint n = 0;
    EGLImageKHR a, b, bad;
    EGLSyncKHR fence;
    GLuint rba, rbb, fba, fbb, tex, prog;
    unsigned char *mema, *memb, *big, *copy;
    unsigned char px[4];
    const char *ext;
    int i, frames = 100, w = 256, h = 64, rb = 1024;
    double t0, t_into, t_read;

    out = stdout;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            FILE *f = fopen(argv[++i], "w");
            if (f)
                out = f;
        } else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            frames = atoi(argv[++i]);
        }
    }
    if (getenv("V3DM$Fake"))
        drawn = 0;
    fprintf(out, "gpuimage: the GPU drawing into the program's memory (EGL_RISCOS_image_memory)\n");

    get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
    dpy = get_platform_display ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL)
                               : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    CHECK(dpy != EGL_NO_DISPLAY && eglInitialize(dpy, NULL, NULL), "a display");
    ext = eglQueryString(dpy, EGL_EXTENSIONS);
    fprintf(out, "      EGL_VERSION: %s\n", eglQueryString(dpy, EGL_VERSION));
    if (!ext || !strstr(ext, "EGL_RISCOS_image_memory")) {
        fprintf(out, "      EGL_RISCOS_image_memory isn't listed: the GPU library doesn't draw here "
                "(software, RISC OS on Linux, or LIBGL_ALWAYS_SOFTWARE / RISC3D$Disable)\n");
        fprintf(out, "%d checks, %d failures: %s\n", checks, failures, failures ? "FAILED" : "ALL PASS (not here)");
        return failures != 0;
    }
    CHECK(1, "EGL_RISCOS_image_memory listed");
    create_image = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    destroy_image = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
    image_storage = (PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)
        eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
    image_texture = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
    create_sync = (PFNEGLCREATESYNCKHRPROC) eglGetProcAddress("eglCreateSyncKHR");
    client_wait = (PFNEGLCLIENTWAITSYNCKHRPROC) eglGetProcAddress("eglClientWaitSyncKHR");
    destroy_sync = (PFNEGLDESTROYSYNCKHRPROC) eglGetProcAddress("eglDestroySyncKHR");
    CHECK(create_image && destroy_image && image_storage && image_texture, "the functions");
    if (!create_image || !destroy_image || !image_storage || !image_texture)
        return 1;

    eglBindAPI(EGL_OPENGL_ES_API);
    CHECK(eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &n) && n == 1, "a GLES 2 config");
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, es2);
    CHECK(ctx != EGL_NO_CONTEXT && eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx),
          "a GLES 2 context, current with no surface");
    fprintf(out, "      renderer: %s\n", (const char *) glGetString(GL_RENDERER));

    mema = pages((size_t) rb * h);
    memb = pages((size_t) rb * h);
    if (!mema || !memb)
        return 1;
    bad = image(mema + 16, w, h, rb, FOURCC_AR24);
    CHECK(bad == EGL_NO_IMAGE_KHR && eglGetError() == EGL_BAD_PARAMETER, "memory not 4 KB aligned: EGL_BAD_PARAMETER");
    bad = image(mema, w, h, 1000, FOURCC_AR24);
    CHECK(bad == EGL_NO_IMAGE_KHR && eglGetError() == EGL_BAD_PARAMETER, "rows not a multiple of 16: EGL_BAD_PARAMETER");
    bad = image(mema, w, h, rb, 0x36314752);     /* RG16 */
    CHECK(bad == EGL_NO_IMAGE_KHR && eglGetError() == EGL_BAD_PARAMETER, "another format: EGL_BAD_PARAMETER");

    a = image(mema, w, h, rb, FOURCC_AR24);
    CHECK(a != EGL_NO_IMAGE_KHR, "image A over %dx%d, rows %d bytes, B,G,R,A (error %x)", w, h, rb, eglGetError());
    bad = image(mema, w, h, rb, FOURCC_AR24);
    CHECK(bad == EGL_NO_IMAGE_KHR && eglGetError() == EGL_BAD_ACCESS, "the same memory again: EGL_BAD_ACCESS");
    b = image(memb, w, h, rb, FOURCC_AB24);
    CHECK(b != EGL_NO_IMAGE_KHR, "image B, R,G,B,A");
    if (a == EGL_NO_IMAGE_KHR || b == EGL_NO_IMAGE_KHR)
        return 1;

    fba = framebuffer(a, &rba);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR,
          "A: a framebuffer on it");
    glViewport(0, 0, w, h);
    glClearColor(1, 0, 0, 1);                   /* red */
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, w, 16);                     /* GL's bottom 16 rows green */
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glFinish();
    if (drawn) {
        CHECK(at(mema, rb, 5, 0) == 0x00FF00FFu, "A's memory, row 0: green as B,G,R,A (%08x)", at(mema, rb, 5, 0));
        CHECK(at(mema, rb, 5, h - 1) == 0x0000FFFFu, "A's memory, last row: red (%08x)", at(mema, rb, 5, h - 1));
        CHECK(at(mema, rb, w - 1, 15) == 0x00FF00FFu && at(mema, rb, w - 1, 16) == 0x0000FFFFu,
              "A: rows 0-15 green, from 16 red (GL's y = memory row)");
        CHECK(mema[(size_t) rb * h - 1] == 0xFF && (rb * h) % 4096 == 0, "A: the last byte drawn");
        glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px[0] == 0 && px[1] == 255 && px[2] == 0, "glReadPixels agrees (y 0 green)");
    }

    /* B: A as a texture, drawn over all of B */
    fbb = framebuffer(b, &rbb);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "B: a framebuffer on it");
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    image_texture(GL_TEXTURE_2D, (GLeglImageOES) a);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECK(glGetError() == GL_NO_ERROR, "A as a texture");
    prog = program();
    glUseProgram(prog);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    fence = create_sync ? create_sync(dpy, EGL_SYNC_FENCE_KHR, NULL) : EGL_NO_SYNC_KHR;
    CHECK(fence != EGL_NO_SYNC_KHR && client_wait(dpy, fence, 0, EGL_FOREVER_KHR) == EGL_CONDITION_SATISFIED_KHR,
          "a fence, waited for");
    if (fence != EGL_NO_SYNC_KHR)
        destroy_sync(dpy, fence);
    if (drawn) {
        CHECK(at(memb, rb, 5, 0) == 0x00FF00FFu, "B's memory, row 0: A's green as R,G,B,A (%08x)", at(memb, rb, 5, 0));
        CHECK(at(memb, rb, 5, h - 1) == 0xFF0000FFu, "B's memory, last row: A's red as R,G,B,A (%08x)",
              at(memb, rb, 5, h - 1));
    }
    CHECK(glGetError() == GL_NO_ERROR, "no GL errors");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fba);
    glDeleteFramebuffers(1, &fbb);
    glDeleteRenderbuffers(1, &rba);
    glDeleteRenderbuffers(1, &rbb);
    glDeleteTextures(1, &tex);
    CHECK(destroy_image(dpy, a) && destroy_image(dpy, b), "the images destroyed");
    CHECK(!destroy_image(dpy, a) && eglGetError() == EGL_BAD_PARAMETER, "destroyed again: EGL_BAD_PARAMETER");

    /* 1024x768: drawn into memory, against drawn and read back */
    w = 1024;
    h = 768;
    rb = w * 4;
    big = pages((size_t) rb * h);
    copy = pages((size_t) rb * h);
    a = big ? image(big, w, h, rb, FOURCC_AR24) : EGL_NO_IMAGE_KHR;
    CHECK(a != EGL_NO_IMAGE_KHR, "a %dx%d image", w, h);
    if (a != EGL_NO_IMAGE_KHR && copy && frames > 0) {
        fba = framebuffer(a, &rba);
        glViewport(0, 0, w, h);
        glUseProgram(0);
        t0 = now();
        for (i = 0; i < frames; i++) {
            glClearColor((float) (i & 1), 0.5f, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glFinish();
        }
        t_into = (now() - t0) * 1000 / frames;
        t0 = now();
        for (i = 0; i < frames; i++) {
            glClearColor((float) (i & 1), 0.5f, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, copy);
        }
        t_read = (now() - t0) * 1000 / frames;
        fprintf(out, "      %dx%d, %d frames: %.2f ms a frame drawn into memory, %.2f ms drawn and read back\n",
                w, h, frames, t_into, t_read);
        if (drawn)
            CHECK(at(big, rb, 100, 100) == ((frames - 1) & 1 ? 0x0080FFFFu : 0x008000FFu) ||
                  at(big, rb, 100, 100) == ((frames - 1) & 1 ? 0x007FFFFFu : 0x007F00FFu),
                  "the last frame in memory (%08x)", at(big, rb, 100, 100));
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fba);
        glDeleteRenderbuffers(1, &rba);
        destroy_image(dpy, a);
    }

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    if (drawn == 0)
        fprintf(out, "      (V3DM$Fake: nothing drawn, the pictures aren't checked)\n");
    fprintf(out, "%d checks, %d failures: %s\n", checks, failures, failures ? "FAILED" : "ALL PASS");
    return failures != 0;
}
