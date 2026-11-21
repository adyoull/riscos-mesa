/*
 * gpuprobe.c - what the GPU gives programs through riscos-mesa: for each
 * API (OpenGL, OpenGL ES at its highest, OpenGL ES 1.1), the route a
 * context takes (RISC3D's driver, or riscos-mesa's software renderer),
 * GL_VERSION, GL_RENDERER, the shading language version, the largest
 * texture and the extensions. For RISC3D's setup window (RISC3DSetup),
 * which runs it through !MesaLibs.GPUProbe; it needs no window and no
 * other program running.
 *
 *   gpuprobe [-o file]
 *
 * Output, one "key: value" per line (format 1; a reader ignores keys it
 * doesn't know):
 *
 *   gpuprobe: 1
 *   api: OpenGL | OpenGL ES | OpenGL ES 1
 *   route: RISC3D | software | none
 *   version: ...                (GL_VERSION)
 *   renderer: ...               (GL_RENDERER)
 *   shading: ...                (GL_SHADING_LANGUAGE_VERSION; absent for ES 1)
 *   max_texture_size: N
 *   extensions: A B C ...       (one line, space separated)
 *   end
 *   (the next api...)
 *
 * route "none": no context of that API could be made (error: says why).
 * RISC3D decides which: the GPU when it has a driver for this machine
 * (V3DRender on a Pi 4 natively, Linux3D on RISC OS on Linux), software
 * otherwise or with LIBGL_ALWAYS_SOFTWARE / RISC3D$Disable set.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_riscos.h>
#include <GLES2/gl2.h>

#ifndef GL_SHADING_LANGUAGE_VERSION
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#endif
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif

static FILE *out;
static EGLDisplay dpy;

static const char *str(GLenum name)
{
    const char *s = (const char *) glGetString(name);
    return s ? s : "";
}

/* one API: its context, made current with no surface (a 1x1 pbuffer if
   that is refused), and what it says */
static void probe(const char *title, EGLenum api, EGLint renderable, int es)
{
    EGLint cfg_attrs[] = { EGL_RENDERABLE_TYPE, renderable, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE };
    EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, es, EGL_NONE };
    EGLint pb_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLConfig cfg;
    EGLint n = 0;
    EGLContext ctx;
    EGLSurface pb = EGL_NO_SURFACE;
    GLint max_tex = 0;
    int route;

    fprintf(out, "api: %s\n", title);
    eglBindAPI(api);
    route = riscos_mesa_pick_renderer(api, es == 1 ? 1 : 0, 0);
    if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &n) || n < 1) {
        fprintf(out, "route: none\nerror: no config (EGL error %x)\nend\n", eglGetError());
        return;
    }
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, es ? ctx_attrs : NULL);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(out, "route: none\nerror: no context (EGL error %x)\nend\n", eglGetError());
        return;
    }
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        pb = eglCreatePbufferSurface(dpy, cfg, pb_attrs);
        if (pb == EGL_NO_SURFACE || !eglMakeCurrent(dpy, pb, pb, ctx)) {
            fprintf(out, "route: none\nerror: can't make it current (EGL error %x)\nend\n", eglGetError());
            eglDestroyContext(dpy, ctx);
            return;
        }
    }
    fprintf(out, "route: %s\n", route == RISCOS_MESA_RISC3D ? "RISC3D" : "software");
    fprintf(out, "version: %s\n", str(GL_VERSION));
    fprintf(out, "renderer: %s\n", str(GL_RENDERER));
    if (es != 1)
        fprintf(out, "shading: %s\n", str(GL_SHADING_LANGUAGE_VERSION));
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
    fprintf(out, "max_texture_size: %d\n", (int) max_tex);
    fprintf(out, "extensions: %s\n", str(GL_EXTENSIONS));
    fprintf(out, "end\n");
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (pb != EGL_NO_SURFACE)
        eglDestroySurface(dpy, pb);
    eglDestroyContext(dpy, ctx);
}

int main(int argc, char **argv)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;
    int i;

    out = stdout;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out = fopen(argv[++i], "w");
            if (!out) {
                fprintf(stderr, "gpuprobe: can't write %s\n", argv[i]);
                return 1;
            }
        }
    fprintf(out, "gpuprobe: 1\n");
    get_platform_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
    dpy = get_platform_display ? get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, NULL, NULL)
                               : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
        fprintf(out, "error: no EGL display\n");
        return 1;
    }
    fprintf(out, "egl: %s\n", eglQueryString(dpy, EGL_VERSION));
    probe("OpenGL", EGL_OPENGL_API, EGL_OPENGL_BIT, 0);
    probe("OpenGL ES", EGL_OPENGL_ES_API, EGL_OPENGL_ES2_BIT, 2);   /* the highest ES */
    probe("OpenGL ES 1", EGL_OPENGL_ES_API, EGL_OPENGL_ES_BIT, 1);
    eglTerminate(dpy);
    if (out != stdout)
        fclose(out);
    return 0;
}
