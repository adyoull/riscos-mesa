/*
 * oomtest.c - what a program sees when the GPU module refuses a buffer:
 * GL_OUT_OF_MEMORY from glGetError, and no crash (riscos-mesa 20.3.5-24).
 *
 * Makes 512x512 textures one after another, drawing with each, until
 * glGetError says GL_OUT_OF_MEMORY (or -n of them), then goes on drawing
 * for a while, frees them, and draws again. Run it with *Set V3DM$Refuse
 * <n> to have the GPU library refuse every new buffer after n more (a
 * full GPU memory, without filling it), or without, to fill it for real.
 *
 *   oomtest [-n textures] [-o file]
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GL/gl.h>

#define TEX 512
static FILE *out;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out ? out : stdout, fmt, ap);
    va_end(ap);
    if (out) fflush(out);
}

static void draw(GLuint t)
{
    glBindTexture(GL_TEXTURE_2D, t);
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
}

int main(int argc, char **argv)
{
    static GLuint tex[4096];
    static unsigned px[TEX * TEX];
    int max = 1000, i, n = 0, oom_at = -1, errs_after = 0, ok_after = 0;
    unsigned rgba[4] = { 0, 0, 0, 0 };
    EGLDisplay dpy;
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface pb;
    EGLint nc, ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                        EGL_RED_SIZE, 8, EGL_NONE };
    EGLint pa[] = { EGL_WIDTH, 256, EGL_HEIGHT, 256, EGL_NONE };
    GLenum e;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-n") && i + 1 < argc) max = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = fopen(argv[++i], "w");
    }
    if (max > 4096) max = 4096;
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    eglBindAPI(EGL_OPENGL_API);
    if (!eglChooseConfig(dpy, ca, &cfg, 1, &nc) || nc < 1) { say("no config\n"); return 1; }
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    pb = eglCreatePbufferSurface(dpy, cfg, pa);
    if (!eglMakeCurrent(dpy, pb, pb, ctx)) { say("EGL failed (0x%x)\n", eglGetError()); return 1; }
    say("GL_RENDERER %s; V3DM$Refuse %s\n", (const char *) glGetString(GL_RENDERER),
        getenv("V3DM$Refuse") ? getenv("V3DM$Refuse") : "(unset)");

    for (i = 0; i < TEX * TEX; i++) px[i] = 0xFF000000u | (i * 2654435761u >> 8);
    for (n = 0; n < max; n++) {
        glGenTextures(1, &tex[n]);
        glBindTexture(GL_TEXTURE_2D, tex[n]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX, TEX, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glClearColor(0.2f, 0.3f, 0.5f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        draw(tex[n]);
        glFinish();
        e = glGetError();
        if (e == GL_OUT_OF_MEMORY) { oom_at = n; n++; break; }
        if (e != GL_NO_ERROR) say("texture %d: GL error 0x%x\n", n, e);
    }
    if (oom_at >= 0)
        say("GL_OUT_OF_MEMORY at texture %d (%d KB of textures)\n", oom_at, oom_at * TEX * TEX * 4 / 1024);
    else
        say("no GL_OUT_OF_MEMORY in %d textures (%d KB)\n", n, n * TEX * TEX * 4 / 1024);

    /* go on drawing: nothing should crash */
    for (i = 0; i < 50; i++) {
        glClear(GL_COLOR_BUFFER_BIT);
        draw(tex[i % n]);
        glFinish();
        if (glGetError() != GL_NO_ERROR) errs_after++;
    }
    say("50 more frames drawn after it, %d with errors: no crash\n", errs_after);

    /* free them, and draw again with what's left */
    glDeleteTextures(n, tex);
    glFinish();
    while (glGetError() != GL_NO_ERROR)
        ;
    for (i = 0; i < 10; i++) {
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        if (glGetError() == GL_NO_ERROR) ok_after++;
    }
    glReadPixels(128, 128, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    say("after freeing them: %d of 10 frames with no error; centre pixel &%08X%s\n", ok_after, rgba[0],
        (rgba[0] & 0xFFFFFF) == 0x00FF00 ? " (green: drawn)" : " (not drawn: the memory is still refused)");
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    say("done\n");
    if (out) fclose(out);
    return 0;
}
