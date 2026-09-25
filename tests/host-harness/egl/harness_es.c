/*
 * harness_es.c - OpenGL ES contexts and the DispmanX compatibility layer,
 * compiled the way Raspberry Pi code is: bcm_host.h first (so
 * EGLNativeWindowType is a pointer), then the EGL and GLES headers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "bcm_host.h"
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <EGL/eglext_riscos.h>

#include "fake_riscos.h"

extern int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define RGB(p) ((p) & 0x00FFFFFFu)
#define RED_TBGR   0x0000FFu
#define GREEN      0x00FF00u

/* ES 1.1 fixed-point / float entry points (GLES/gl.h would clash with gl2.h) */
extern void glOrthof(float, float, float, float, float, float);
extern void glMatrixMode(GLenum);
extern void glLoadIdentity(void);
extern void glColor4f(float, float, float, float);
extern void glEnableClientState(GLenum);
extern void glVertexPointer(GLint, GLenum, GLsizei, const void *);

static int box(int x0, int y0, int w, int h, unsigned int v)
{
    int x, y;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            if (RGB(fake_screen_pixel(x, y)) != v) return 0;
    return 1;
}

static EGLConfig es_config(EGLDisplay dpy, EGLint bit)
{
    EGLint a[] = { EGL_RENDERABLE_TYPE, bit, EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                   EGL_DEPTH_SIZE, 16, EGL_NONE };
    EGLConfig c = NULL;
    EGLint n = 0;
    eglChooseConfig(dpy, a, &c, 1, &n);
    return n ? c : NULL;
}

void test_gles(EGLDisplay dpy)
{
    EGLint es2a[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint es3a[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLint prof[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE };
    EGLint pa[] = { EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE };
    EGLConfig cfg;
    EGLContext c2, c1, glc;
    EGLSurface pb;
    EGLint n = 0, v = 0;
    const char *s;
    unsigned char px[4];

    CHECK(strcmp(eglQueryString(dpy, EGL_CLIENT_APIS), "OpenGL OpenGL_ES") == 0, "client APIs");
    CHECK(eglChooseConfig(dpy, NULL, NULL, 0, &n) && n == 8, "default choose (ES) finds all 8 (%d)", n);
    cfg = es_config(dpy, EGL_OPENGL_ES2_BIT);
    CHECK(cfg != NULL, "ES2 config");
    glc = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);     /* GL bound: desktop */
    CHECK(eglBindAPI(EGL_OPENGL_ES_API) && eglQueryAPI() == EGL_OPENGL_ES_API, "bind ES");
    CHECK(eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, es3a) == EGL_NO_CONTEXT &&
          eglGetError() == EGL_BAD_MATCH, "ES 3 refused");
    CHECK(eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, prof) == EGL_NO_CONTEXT &&
          eglGetError() == EGL_BAD_ATTRIBUTE, "profile mask refused for ES");
    CHECK(eglCreateContext(dpy, cfg, glc, es2a) == EGL_NO_CONTEXT &&
          eglGetError() == EGL_BAD_MATCH, "no sharing with a GL context");
    c2 = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, es2a);
    c1 = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);      /* default: ES 1 */
    CHECK(c2 != EGL_NO_CONTEXT && c1 != EGL_NO_CONTEXT, "ES 2 and ES 1 contexts");
    eglQueryContext(dpy, c2, EGL_CONTEXT_CLIENT_TYPE, &v);
    CHECK(v == EGL_OPENGL_ES_API, "client type ES");
    eglQueryContext(dpy, c2, EGL_CONTEXT_CLIENT_VERSION, &v);
    CHECK(v == 2, "client version 2 (%d)", v);
    eglQueryContext(dpy, c1, EGL_CONTEXT_CLIENT_VERSION, &v);
    CHECK(v == 1, "client version 1 (%d)", v);

    pb = eglCreatePbufferSurface(dpy, cfg, pa);
    CHECK(eglMakeCurrent(dpy, pb, pb, c2), "ES 2 current");
    s = (const char *) glGetString(GL_VERSION);
    CHECK(s && strncmp(s, "OpenGL ES 2.0", 13) == 0, "ES 2.0 (%s)", s ? s : "null");
    {
        const char *vs = "#version 100\nattribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }";
        const char *fs = "#version 100\nprecision mediump float; uniform vec4 col;\n"
                         "void main() { gl_FragColor = col; }";
        static const float tri[] = { -1, -1, 3, -1, -1, 3 };
        GLuint vsh = glCreateShader(GL_VERTEX_SHADER), fsh = glCreateShader(GL_FRAGMENT_SHADER);
        GLuint prog = glCreateProgram();
        GLint ok = 0;
        glShaderSource(vsh, 1, &vs, NULL); glCompileShader(vsh);
        glShaderSource(fsh, 1, &fs, NULL); glCompileShader(fsh);
        glAttachShader(prog, vsh); glAttachShader(prog, fsh);
        glBindAttribLocation(prog, 0, "p");
        glLinkProgram(prog);
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        CHECK(ok, "GLSL ES 1.00 program links");
        glUseProgram(prog);
        glUniform4f(glGetUniformLocation(prog, "col"), 0, 1, 0, 1);
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px[0] == 0 && px[1] == 255 && glGetError() == GL_NO_ERROR, "ES 2 shader draw (%d %d %d)",
              px[0], px[1], px[2]);
        {
            /* Pi code (hello_triangle2) leaves out the fragment shader's
               default float precision; the VideoCore compiler accepted
               that, and our Mesa patch takes mediump instead of failing. */
            const char *fs2 = "uniform vec4 col; void main() { float a = 0.5; gl_FragColor = col * a; }";
            GLuint f2 = glCreateShader(GL_FRAGMENT_SHADER);
            glShaderSource(f2, 1, &fs2, NULL); glCompileShader(f2);
            glGetShaderiv(f2, GL_COMPILE_STATUS, &ok);
            CHECK(ok, "fragment shader with no default precision compiles");
            glDeleteShader(f2);
        }
    }
    CHECK(eglGetCurrentContext() == c2, "current ES context");
    eglBindAPI(EGL_OPENGL_API);
    CHECK(eglGetCurrentContext() == EGL_NO_CONTEXT, "no current GL context");
    eglBindAPI(EGL_OPENGL_ES_API);

    CHECK(eglMakeCurrent(dpy, pb, pb, c1), "ES 1 current");
    s = (const char *) glGetString(GL_VERSION);
    CHECK(s && strncmp(s, "OpenGL ES-CM 1.1", 16) == 0, "ES 1.1 (%s)", s ? s : "null");
    {
        static const float tri[] = { -1, -1, 3, -1, -1, 3 };
        glClearColor(0, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(0x1701);           /* GL_PROJECTION */
        glLoadIdentity();
        glOrthof(-1, 1, -1, 1, -1, 1);
        glColor4f(1, 1, 0, 1);
        glEnableClientState(0x8074);    /* GL_VERTEX_ARRAY */
        glVertexPointer(2, GL_FLOAT, 0, tri);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glReadPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px[0] == 255 && px[1] == 255 && px[2] == 0, "ES 1.1 fixed function draw");
    }
    /* OpenGL ES on the native RISC OS types: a Wimp window and a sprite */
    {
        EGLSurface ws, ps;
        int *area = malloc(16 + 44 + 16 * 8 * 4), *spr = area + 4;
        unsigned int *pix = (unsigned int *) ((char *) spr + 44);
        fake_open_window(0x1000, 200, 300, 400, 460, 0, 0);    /* 100x80 at pixel (100, 250) */
        ws = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType) (intptr_t) 0x1000, NULL);
        CHECK(ws != EGL_NO_SURFACE && eglMakeCurrent(dpy, ws, ws, c2), "ES 2 on a Wimp window");
        memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(dpy, ws);
        CHECK(box(100, 250, 100, 80, GREEN), "ES 2 frame in the window");
        area[0] = 16 + 44 + 16 * 8 * 4; area[1] = 1; area[2] = 16; area[3] = area[0];
        memset(spr, 0, 44 + 16 * 8 * 4);
        spr[0] = 44 + 16 * 8 * 4; spr[4] = 15; spr[5] = 7; spr[7] = 31; spr[8] = 44; spr[9] = 44;
        spr[10] = 1 | (90 << 1) | (90 << 14) | (6 << 27);
        ps = eglCreatePixmapSurface(dpy, cfg, spr, NULL);
        CHECK(ps != EGL_NO_SURFACE && eglMakeCurrent(dpy, ps, ps, c1), "ES 1 on a sprite");
        glClearColor(1, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glFinish();
        CHECK(RGB(pix[0]) == RED_TBGR && RGB(pix[16 * 8 - 1]) == RED_TBGR, "ES 1 drew into the sprite");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, ws);
        eglDestroySurface(dpy, ps);
        free(area);
    }

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, pb);
    eglDestroyContext(dpy, c1);
    eglDestroyContext(dpy, c2);
    eglBindAPI(EGL_OPENGL_API);
    eglDestroyContext(dpy, glc);
}

/* The hello_pi way of getting a window, then checks on where it shows. */
void test_dispmanx(EGLDisplay dpy)
{
    static EGL_DISPMANX_WINDOW_T nw, bad;
    DISPMANX_DISPLAY_HANDLE_T disp;
    DISPMANX_UPDATE_HANDLE_T upd;
    DISPMANX_ELEMENT_HANDLE_T el;
    DISPMANX_MODEINFO_T info;
    VC_RECT_T dst, src;
    uint32_t w = 0, h = 0;
    EGLint es2a[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface ws;
    EGLint sw = 0, sh = 0;
    int plots, forced;

    bcm_host_init();
    CHECK(graphics_get_display_size(0, &w, &h) >= 0 && w == 640 && h == 480, "display size %ux%u", w, h);
    disp = vc_dispmanx_display_open(0);
    CHECK(disp != DISPMANX_NO_HANDLE, "display open");
    CHECK(vc_dispmanx_display_get_info(disp, &info) == 0 && info.width == 640 && info.height == 480,
          "display info");
    upd = vc_dispmanx_update_start(0);
    vc_dispmanx_rect_set(&dst, 100, 50, 200, 150);
    vc_dispmanx_rect_set(&src, 0, 0, 100 << 16, 75 << 16);
    el = vc_dispmanx_element_add(upd, disp, 0, &dst, 0, &src, DISPMANX_PROTECTION_NONE, 0, 0, 0);
    CHECK(el != DISPMANX_NO_HANDLE, "element added");
    CHECK(vc_dispmanx_update_submit_sync(upd) == 0, "update submitted");
    nw.element = el; nw.width = 100; nw.height = 75;

    eglBindAPI(EGL_OPENGL_ES_API);
    cfg = es_config(dpy, EGL_OPENGL_ES2_BIT);
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, es2a);
    bad.element = 0x1234; bad.width = 10; bad.height = 10;
    CHECK(eglCreateWindowSurface(dpy, cfg, &bad, NULL) == EGL_NO_SURFACE &&
          eglGetError() == EGL_BAD_NATIVE_WINDOW, "unknown element refused");
    CHECK(eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType) 0x70000000, NULL) == EGL_NO_SURFACE &&
          eglGetError() == EGL_BAD_NATIVE_WINDOW, "unreadable pointer refused");
    /* The native RISC OS types still work in a program built for DispmanX
       (EGLNativeWindowType is a pointer here, so they need a cast). */
    {
        EGLSurface wimp, scr;
        EGLint ww = 0;
        fake_open_window(0x1000, 200, 300, 400, 460, 0, 0);
        wimp = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType) (intptr_t) 0x1000, NULL);
        scr = eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, NULL);
        eglQuerySurface(dpy, wimp, EGL_WIDTH, &ww);
        CHECK(wimp != EGL_NO_SURFACE && ww == 100, "Wimp window alongside DispmanX (width %d)", ww);
        eglQuerySurface(dpy, scr, EGL_WIDTH, &ww);
        CHECK(scr != EGL_NO_SURFACE && ww == 640, "whole screen alongside DispmanX");
        eglDestroySurface(dpy, wimp);
        eglDestroySurface(dpy, scr);
    }
    ws = eglCreateWindowSurface(dpy, cfg, &nw, NULL);
    CHECK(ws != EGL_NO_SURFACE, "DispmanX window surface");
    eglQuerySurface(dpy, ws, EGL_WIDTH, &sw);
    eglQuerySurface(dpy, ws, EGL_HEIGHT, &sh);
    CHECK(sw == 100 && sh == 75, "surface is the window's size %dx%d", sw, sh);
    CHECK(eglMakeCurrent(dpy, ws, ws, ctx), "current");

    /* red, with a green 10x10 marker at the surface's top left */
    memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 65, 10, 10);
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    fake_vsyncs = 0;
    plots = fake_scaled_plots;
    CHECK(eglSwapBuffers(dpy, ws), "swap");
    CHECK(fake_scaled_plots == plots + 1 && fake_vsyncs == 1, "one scaled plot after the vsync wait");
    CHECK(box(100, 50, 20, 20, GREEN), "marker at the element's top left, scaled 2x");
    CHECK(box(120, 50, 180, 150, RED_TBGR) && box(100, 70, 200, 130, RED_TBGR), "rest red, 200x150");
    CHECK(RGB(fake_screen_pixel(99, 50)) == 0 && RGB(fake_screen_pixel(100, 49)) == 0 &&
          RGB(fake_screen_pixel(300, 100)) == 0 && RGB(fake_screen_pixel(150, 200)) == 0,
          "nothing outside the destination");

    /* moving it redraws the desktop at the old place */
    forced = fake_force_redraws;
    upd = vc_dispmanx_update_start(0);
    vc_dispmanx_rect_set(&dst, 0, 0, 640, 480);
    CHECK(vc_dispmanx_element_change_attributes(upd, el, ELEMENT_CHANGE_DEST_RECT, 0, 255,
                                                 &dst, NULL, 0, 0) == 0, "change destination");
    vc_dispmanx_update_submit_sync(upd);
    CHECK(fake_force_redraws == forced + 1 && fake_force_rect[0] == -1 &&
          fake_force_rect[1] == 200 && fake_force_rect[2] == 560 &&
          fake_force_rect[3] == 600 && fake_force_rect[4] == 860,
          "old area redrawn (%d %d %d %d)", fake_force_rect[1], fake_force_rect[2],
          fake_force_rect[3], fake_force_rect[4]);
    memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
    eglSwapBuffers(dpy, ws);
    CHECK(box(0, 0, 64, 64, GREEN) && RGB(fake_screen_pixel(70, 70)) == RED_TBGR &&
          RGB(fake_screen_pixel(639, 479)) == RED_TBGR, "scaled to the whole screen (6.4x)");

    /* buffer age: the sprite keeps the last frame */
    {
        EGLint age = -1;
        eglQuerySurface(dpy, ws, EGL_BUFFER_AGE_EXT, &age);
        CHECK(age == 1, "buffer age 1 (%d)", age);
    }

    /* removing it redraws the desktop and stops the plots */
    forced = fake_force_redraws;
    upd = vc_dispmanx_update_start(0);
    CHECK(vc_dispmanx_element_remove(upd, el) == 0 && fake_force_redraws == forced + 1,
          "remove: desktop redrawn");
    vc_dispmanx_update_submit_sync(upd);
    plots = fake_plots;
    CHECK(eglSwapBuffers(dpy, ws) && fake_plots == plots, "swap after removal shows nothing");
    CHECK(vc_dispmanx_element_remove(upd, el) == -1, "removed twice");

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, ws);
    eglDestroyContext(dpy, ctx);
    vc_dispmanx_display_close(disp);
    eglBindAPI(EGL_OPENGL_API);
}
