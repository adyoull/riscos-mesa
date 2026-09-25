/*
 * dmxtest - OpenGL ES through the DispmanX compatibility layer, written the
 * way Raspberry Pi Khronos programs are (hello_triangle style): bcm_host,
 * a DispmanX element, a pointer to an EGL_DISPMANX_WINDOW_T, then EGL and
 * OpenGL ES. It doesn't multitask (Pi programs don't): it runs over the
 * desktop for a few seconds and then the desktop is redrawn.
 *
 * Usage: dmxtest [-2] [-n | -s] [-t secs] [-o file]
 *   default  ES 1.1 spinning lit cube, half-resolution surface scaled 2x to
 *            the whole screen (the usual Pi trick)
 *   -2       ES 2.0 with GLSL ES shaders instead (slower: shaders run on
 *            the CPU)
 *   -n       native resolution surface
 *   -s       small: a 320x240 surface shown 640x480 in the middle
 *   -t secs  run time (default 5), -o file: save the summary instead of
 *            printing it
 * Build with es_cube.c. Link: -lbcm_host -lEGL -lOSMesa -lstdc++ -lz -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "bcm_host.h"
#include <EGL/egl.h>
#include <GLES/gl.h>
#include "hrtime.h"
#include "es_cube.h"

static FILE *outf;
static void say(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (outf)
        fputs(line, outf);      /* no printing then: from the desktop it opens a command window */
    else
        fputs(line, stdout);
}

int main(int argc, char **argv)
{
    static EGL_DISPMANX_WINDOW_T nativewindow;
    DISPMANX_DISPLAY_HANDLE_T dispman_display;
    DISPMANX_UPDATE_HANDLE_T dispman_update;
    DISPMANX_ELEMENT_HANDLE_T dispman_element;
    VC_RECT_T dst_rect, src_rect;
    uint32_t screen_w, screen_h, surf_w, surf_h, dst_w, dst_h, dst_x = 0, dst_y = 0;
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface;
    EGLint num_config, ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE };
    static const EGLint attr[] = { EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                                   EGL_DEPTH_SIZE, 16, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE };
    int es2 = 0, native = 0, small = 0, i, frames = 0;
    double limit = 5, t0, t1, render = 0, t_start;
    float angle = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-2")) es2 = 1;
        else if (!strcmp(argv[i], "-n")) native = 1;
        else if (!strcmp(argv[i], "-s")) small = 1;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) limit = atof(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) outf = fopen(argv[++i], "w");
        else { printf("usage: dmxtest [-2] [-n | -s] [-t secs] [-o file]\n"); return 1; }
    }
    /* --- the Raspberry Pi way --- */
    bcm_host_init();
    if (graphics_get_display_size(0, &screen_w, &screen_h) < 0) return 1;
    if (small) {
        surf_w = 320; surf_h = 240; dst_w = 640; dst_h = 480;
        dst_x = (screen_w - dst_w) / 2; dst_y = (screen_h - dst_h) / 2;
    } else if (native) {
        surf_w = dst_w = screen_w; surf_h = dst_h = screen_h;
    } else {
        surf_w = screen_w / 2; surf_h = screen_h / 2; dst_w = screen_w; dst_h = screen_h;
    }
    vc_dispmanx_rect_set(&dst_rect, dst_x, dst_y, dst_w, dst_h);
    vc_dispmanx_rect_set(&src_rect, 0, 0, surf_w << 16, surf_h << 16);
    dispman_display = vc_dispmanx_display_open(0 /* LCD */);
    dispman_update = vc_dispmanx_update_start(0);
    dispman_element = vc_dispmanx_element_add(dispman_update, dispman_display, 0, &dst_rect,
                                              0, &src_rect, DISPMANX_PROTECTION_NONE, 0, 0, 0);
    nativewindow.element = dispman_element;
    nativewindow.width = surf_w;
    nativewindow.height = surf_h;
    vc_dispmanx_update_submit_sync(dispman_update);

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(display, NULL, NULL)) return 1;
    if (!eglChooseConfig(display, attr, &config, 1, &num_config) || num_config < 1) return 1;
    eglBindAPI(EGL_OPENGL_ES_API);
    ctx_attr[1] = es2 ? 2 : 1;
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attr);
    surface = eglCreateWindowSurface(display, config, &nativewindow, NULL);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !eglMakeCurrent(display, surface, surface, context)) {
        printf("EGL setup failed (0x%04x)\n", eglGetError());
        return 1;
    }
    say("dmxtest: %s, surface %ux%u shown at %u,%u %ux%u\n",
        (const char *) glGetString(GL_VERSION), (unsigned) surf_w, (unsigned) surf_h,
        (unsigned) dst_x, (unsigned) dst_y, (unsigned) dst_w, (unsigned) dst_h);

    if (!es_cube_init(es2, surf_w, surf_h)) {
        printf("shader link failed\n");
        return 1;
    }

    t_start = hr_seconds();
    while (hr_seconds() - t_start < limit) {
        t0 = hr_seconds();
        es_cube_draw(angle);
        glFinish();
        t1 = hr_seconds();
        render += t1 - t0;
        eglSwapBuffers(display, surface);
        frames++;
        angle += 2;
    }
    {
        double t = hr_seconds() - t_start;
        say("%d frames in %.1f s = %.1f fps, render %.2f ms per frame\n", frames, t,
            frames / (t > 0 ? t : 1), frames ? 1000 * render / frames : 0);
    }

    /* --- tidy up the Pi way; the desktop underneath is redrawn --- */
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(display, surface);
    eglDestroyContext(display, context);
    eglTerminate(display);
    dispman_update = vc_dispmanx_update_start(0);
    vc_dispmanx_element_remove(dispman_update, dispman_element);
    vc_dispmanx_update_submit_sync(dispman_update);
    vc_dispmanx_display_close(dispman_display);
    if (outf) fclose(outf);
    return 0;
}
