/*
 * egltest - checks riscos-mesa's EGL (egl/egl_riscos.c) on RISC OS.
 *
 * Usage:
 *   egltest [-o file]            checks without the desktop: strings, configs,
 *                                pbuffer and pixmap (sprite) rendering, a
 *                                sprite used as a texture (EGLImage), error
 *                                cases. Saves the pixmap as Sprite file
 *                                "eglpixmap". Prints PASS/FAIL.
 *   egltest -w [-r] [-t secs] [-o file]
 *                                desktop window with a spinning cube, fps in
 *                                the title. Resize, scroll and cover it.
 *                                -r adds a second, fixed-size EGL surface at a
 *                                work area position (EGL_RISCOS_wimp_window).
 *   egltest -f [-d] [-b n] [-v n] [-p] [-t secs] [-o file]
 *                                full screen (native window -1) for 5 s or -t;
 *                                -d renders straight into screen memory
 *                                (EGL_SINGLE_BUFFER), -v swap interval (1),
 *                                -b 2|3 screen banks (experimental),
 *                                -p a sweeping bar instead of the cube
 *                                (makes tearing easy to see).
 *   egltest -w -R                 as -r, but the second surface has its own
 *                                GL context.
 *   egltest -w -V|-n             -V asks for a hardware overlay
 *                                (EGL_RISCOS_overlay: EGL_OVERLAY_RISCOS
 *                                true), used when VideoOverlay is loaded;
 *                                -n refuses one (false, beats EGL$Overlay
 *                                on). Neither: EGL$Overlay decides (off
 *                                unless it's "on").
 *                                Click in the window, then H switches the
 *                                overlay off and on, P pauses (the paused
 *                                frame stays; eglCheckOverlaysRISCOS on null
 *                                events hides it under windows and menus).
 *                                The title and summary show which is used.
 *   egltest -w|-f -S WxH         render at WxH and scale to the window or
 *                                screen (EGL_RENDER_WIDTH/HEIGHT_RISCOS): by
 *                                the overlay with -w -V, else by the plot.
 *                                In a window, S switches between that and
 *                                the window's own size.
 *   egltest -f -k                 full screen, and before each swap asks
 *                                eglSwapWouldWaitRISCOS: while it says a
 *                                swap would wait, does 0.5 ms pieces of
 *                                "other work" (as a video player decodes).
 *                                The summary shows the time a frame spent
 *                                blocked in the swap, and the time freed
 *                                for other work.
 *   egltest -w -D                 damage demo: after the first frame only the
 *                                middle of the window is redrawn and shown
 *                                (EGL_EXT_buffer_age and
 *                                eglSwapBuffersWithDamageKHR). Its background
 *                                changes colour; the edges keep the first one.
 * The desktop modes print a summary when they finish (printing while a Wimp
 * task pops up a command window); -o also writes it to a file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>
#include <swis.h>

#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_riscos.h>
#include <GL/gl.h>

#include "hrtime.h"

#define TASK 0x4B534154

static FILE *outf;
static char summary[4096];
static size_t summary_len;
static int failures, checks;

/* In the desktop modes, collect output and print it at the end. */
static int collect;

static void say(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (collect) {
        size_t n = strlen(line);
        if (summary_len + n < sizeof summary) {
            memcpy(summary + summary_len, line, n + 1);
            summary_len += n;
        }
    } else {
        fputs(line, stdout);
    }
    if (outf) { fputs(line, outf); fflush(outf); }
}

#define CHECK(cond, what) do { checks++; if (cond) say("  ok   %s\n", what); \
    else { failures++; say("  FAIL %s (EGL error 0x%04x)\n", what, eglGetError()); } } while (0)

/* ------------------------------------------------------------------ */

static void cube(void)
{
    static const float n[6][3] = {{0,0,1},{0,0,-1},{0,1,0},{0,-1,0},{1,0,0},{-1,0,0}};
    static const float c[6][3] = {{1,.3f,.3f},{.3f,1,.3f},{.3f,.3f,1},{1,1,.3f},{1,.3f,1},{.3f,1,1}};
    static const int f[6][4][3] = {
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
        {{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1},{ 1,-1,-1}},
        {{-1, 1,-1},{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1}},
        {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},
        {{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1},{ 1,-1, 1}},
        {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}}};
    int i, j;
    glBegin(GL_QUADS);
    for (i = 0; i < 6; i++) {
        glColor3fv(c[i]);
        glNormal3fv(n[i]);
        for (j = 0; j < 4; j++)
            glVertex3f((float)f[i][j][0], (float)f[i][j][1], (float)f[i][j][2]);
    }
    glEnd();
}

/* Draw the cube at angle a into a w x h viewport. */
static float zoom = 6;             /* camera distance; the scroll wheel changes it */

static void scene(int w, int h, float a, float r, float g, float b)
{
    static const float lpos[4] = {2, 3, 4, 0};
    glViewport(0, 0, w, h);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glLightfv(GL_LIGHT0, GL_POSITION, lpos);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-(float) w / h, (float) w / h, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -zoom);
    glRotatef(a, 1, 0.7f, 0.3f);
    cube();
}

static EGLDisplay dpy;

static EGLConfig pick_config(EGLint surface_type, EGLint depth)
{
    EGLint attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, surface_type,
                       EGL_DEPTH_SIZE, depth, EGL_NONE };
    EGLConfig cfg = NULL;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n) || n < 1)
        return NULL;
    return cfg;
}

static int egl_start(void)
{
    EGLint major = 0, minor = 0;
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
        say("EGL initialise failed (0x%04x)\n", eglGetError());
        return 0;
    }
    eglBindAPI(EGL_OPENGL_API);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Checks mode                                                         */

static int *make_sprite_area(int w, int h, int **sprite)
{
    int size = 16 + 44 + w * h * 4 + 64;
    int *area = malloc(size);
    _kernel_swi_regs r;
    if (!area) return NULL;
    area[0] = size; area[1] = 0; area[2] = 16; area[3] = 16;
    r.r[0] = 256 + 15;
    r.r[1] = (int) area;
    r.r[2] = (int) "eglpixmap";
    r.r[3] = 0;
    r.r[4] = w;
    r.r[5] = h;
    r.r[6] = 1 | (90 << 1) | (90 << 14) | (6 << 27);
    if (_kernel_swi(OS_SpriteOp, &r, &r)) { free(area); return NULL; }
    r.r[0] = 256 + 24;                  /* select sprite: R2 = its address */
    r.r[1] = (int) area;
    r.r[2] = (int) "eglpixmap";
    if (_kernel_swi(OS_SpriteOp, &r, &r)) { free(area); return NULL; }
    *sprite = (int *) r.r[2];
    return area;
}

static int debug_hits;
static const char *debug_cmd;

static void EGLAPIENTRY debug_cb(EGLenum error, const char *command, EGLint type,
                                 EGLLabelKHR thread, EGLLabelKHR object, const char *message)
{
    (void) error; (void) type; (void) thread; (void) object;
    debug_hits++;
    debug_cmd = command;
    say("  (debug callback: %s: %s)\n", command, message);
}

static int run_checks(void)
{
    EGLConfig cfgs[16], cfg;
    EGLint n = 0, i, v[6];
    EGLSurface pb, ps;
    EGLContext ctx;
    unsigned char px[4];
    int *area, *spr;
    const char *s;

    say("egltest checks\n");
    if (!egl_start()) return 1;
    say("EGL_VENDOR     %s\n", eglQueryString(dpy, EGL_VENDOR));
    say("EGL_VERSION    %s\n", eglQueryString(dpy, EGL_VERSION));
    say("EGL_CLIENT_APIS %s\n", eglQueryString(dpy, EGL_CLIENT_APIS));
    say("EGL_EXTENSIONS %s\n", eglQueryString(dpy, EGL_EXTENSIONS));
    eglGetConfigs(dpy, cfgs, 16, &n);
    say("%d configs (id depth stencil visual):\n", n);
    for (i = 0; i < n; i++) {
        eglGetConfigAttrib(dpy, cfgs[i], EGL_CONFIG_ID, &v[0]);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_DEPTH_SIZE, &v[1]);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_STENCIL_SIZE, &v[2]);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &v[3]);
        say("   %d  %2d %d  %s\n", v[0], v[1], v[2], v[3] ? "TRGB 0x00RRGGBB" : "TBGR 0x00BBGGRR");
    }

    cfg = pick_config(EGL_PBUFFER_BIT | EGL_PIXMAP_BIT, 16);
    CHECK(cfg != NULL, "config with depth for pbuffer + pixmap");
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    CHECK(ctx != EGL_NO_CONTEXT, "create GL context");
    {
        EGLint pa[] = { EGL_WIDTH, 128, EGL_HEIGHT, 64, EGL_NONE };
        pb = eglCreatePbufferSurface(dpy, cfg, pa);
    }
    CHECK(pb != EGL_NO_SURFACE, "pbuffer 128x64");
    CHECK(eglMakeCurrent(dpy, pb, pb, ctx), "make pbuffer current");
    s = (const char *) glGetString(GL_VERSION);
    say("GL_VERSION  %s\nGL_RENDERER %s\n", s ? s : "(null)", (const char *) glGetString(GL_RENDERER));
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0, "pbuffer cleared red, read back");

    area = make_sprite_area(96, 96, &spr);
    CHECK(area != NULL, "32bpp sprite for the pixmap");
    if (area) {
        unsigned int *pix = (unsigned int *) ((char *) spr + spr[8]);
        ps = eglCreatePixmapSurface(dpy, cfg, spr, NULL);
        CHECK(ps != EGL_NO_SURFACE, "pixmap surface on the sprite");
        CHECK(eglMakeCurrent(dpy, ps, ps, ctx), "make pixmap current");
        scene(96, 96, 30, 0.3f, 0, 0);
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 95, 4, 1);                 /* top row: 4 green pixels */
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        eglWaitClient();
        say("sprite pixel (0,0) = %08x, (50,50) = %08x\n", pix[0], pix[50 * 96 + 50]);
        CHECK((pix[0] & 0xFFFFFF) == 0x00FF00, "GL rendered into the sprite (top row green)");
        CHECK((pix[95 * 96] & 0xFF) > 0x40 && (pix[95 * 96] & 0xFFFF00) == 0,
              "background dark red, in 0x00BBGGRR order");
        {
            _kernel_swi_regs r;
            r.r[0] = 256 + 12;                  /* save sprite file */
            r.r[1] = (int) area;
            r.r[2] = (int) "eglpixmap";
            CHECK(_kernel_swi(OS_SpriteOp, &r, &r) == NULL, "saved Sprite file eglpixmap (open in Paint)");
        }
        eglMakeCurrent(dpy, pb, pb, ctx);
        CHECK(eglDestroySurface(dpy, ps), "destroy pixmap surface");
    }

    /* extensions */
    s = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    say("client extensions %s\n", s ? s : "(null)");
    CHECK(s && strstr(s, "EGL_EXT_platform_base"), "client extensions");
    CHECK(eglGetPlatformDisplayEXT(EGL_PLATFORM_RISCOS, NULL, NULL) == dpy, "platform display");
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx) &&
          eglGetCurrentSurface(EGL_DRAW) == EGL_NO_SURFACE, "surfaceless context");
    CHECK(eglMakeCurrent(dpy, pb, pb, ctx), "back to the pbuffer");
    {
        EGLSyncKHR f = eglCreateSyncKHR(dpy, EGL_SYNC_FENCE_KHR, NULL);
        EGLSyncKHR y = eglCreateSyncKHR(dpy, EGL_SYNC_REUSABLE_KHR, NULL);
        CHECK(f != EGL_NO_SYNC_KHR && eglClientWaitSyncKHR(dpy, f, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
              EGL_FOREVER_KHR) == EGL_CONDITION_SATISFIED_KHR, "fence sync");
        CHECK(y != EGL_NO_SYNC_KHR && eglClientWaitSyncKHR(dpy, y, 0, 0) == EGL_TIMEOUT_EXPIRED_KHR &&
              eglSignalSyncKHR(dpy, y, EGL_SIGNALED_KHR) &&
              eglClientWaitSyncKHR(dpy, y, 0, 0) == EGL_CONDITION_SATISFIED_KHR, "reusable sync");
        eglDestroySyncKHR(dpy, f);
        eglDestroySyncKHR(dpy, y);
    }
    {
        EGLint age = -1;
        CHECK(eglQuerySurface(dpy, pb, EGL_BUFFER_AGE_EXT, &age) && age == 0, "buffer age of a pbuffer = 0");
    }
    if (area) {
        EGLSurface ls = eglCreatePixmapSurface(dpy, cfg, spr, NULL);
        EGLint ptr = 0, pitch = 0;
        CHECK(ls != EGL_NO_SURFACE && eglLockSurfaceKHR(dpy, ls, NULL), "lock a pixmap surface");
        eglQuerySurface(dpy, ls, EGL_BITMAP_POINTER_KHR, &ptr);
        eglQuerySurface(dpy, ls, EGL_BITMAP_PITCH_KHR, &pitch);
        CHECK(ptr == (EGLint) ((char *) spr + spr[8]) && pitch == 96 * 4, "bitmap = the sprite's pixels");
        CHECK(eglUnlockSurfaceKHR(dpy, ls) && eglDestroySurface(dpy, ls), "unlock");
    }
    if (area) {
        /* EGL_KHR_image_pixmap + GL_OES_EGL_image: the sprite as a texture,
           used in place (a video player writes each frame into it) */
        typedef void (*TargetTexture_t)(GLenum, void *);
        TargetTexture_t target = (TargetTexture_t) eglGetProcAddress("glEGLImageTargetTexture2DOES");
        const unsigned int *pix = (const unsigned int *) ((char *) spr + spr[8]);
        static unsigned char rgba[96 * 96 * 4];
        EGLImageKHR img;
        GLuint tex;
        int x, y, bad;

        img = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer) spr, NULL);
        CHECK(img != EGL_NO_IMAGE_KHR, "EGLImage from the sprite");
        CHECK(target && strstr((const char *) glGetString(GL_EXTENSIONS), "GL_OES_EGL_image"),
              "GL_OES_EGL_image");
        if (img != EGL_NO_IMAGE_KHR && target) {
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            target(GL_TEXTURE_2D, img);
            CHECK(glGetError() == GL_NO_ERROR, "texture from the image");
            for (i = 0; i < 2; i++) {
                if (i == 1)                     /* a new "frame": no GL call */
                    memset((char *) spr + spr[8], 0x60, 96 * 4 * 10);
                glViewport(0, 0, 96, 96);
                glMatrixMode(GL_PROJECTION); glLoadIdentity();
                glMatrixMode(GL_MODELVIEW); glLoadIdentity();
                glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST);
                glEnable(GL_TEXTURE_2D);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
                glBegin(GL_QUADS);              /* sprite row 0 at the top */
                glTexCoord2f(0, 1); glVertex2f(-1, -1);
                glTexCoord2f(1, 1); glVertex2f(1, -1);
                glTexCoord2f(1, 0); glVertex2f(1, 1);
                glTexCoord2f(0, 0); glVertex2f(-1, 1);
                glEnd();
                glDisable(GL_TEXTURE_2D);
                glReadPixels(0, 0, 96, 96, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                for (bad = 0, y = 0; y < 96; y++)
                    for (x = 0; x < 96; x++) {
                        unsigned int p = pix[(95 - y) * 96 + x];
                        const unsigned char *q = rgba + (y * 96 + x) * 4;
                        if (q[0] != (p & 255) || q[1] != ((p >> 8) & 255) || q[2] != ((p >> 16) & 255))
                            bad++;
                    }
                CHECK(bad == 0, i == 0 ? "textured quad shows the sprite" :
                                         "sprite changed: shows at the next draw, no upload");
                if (bad) say("  (%d pixels differ)\n", bad);
            }
            glDeleteTextures(1, &tex);
            CHECK(eglDestroyImageKHR(dpy, img), "destroy image");
        }
    }
    {
        EGLAttrib on[] = { EGL_DEBUG_MSG_ERROR_KHR, EGL_TRUE, EGL_NONE };
        EGLint v1;
        debug_hits = 0;
        CHECK(eglDebugMessageControlKHR(debug_cb, on) == EGL_SUCCESS, "debug callback set");
        eglQuerySurface(dpy, pb, 0x1234, &v1);
        CHECK(debug_hits == 1 && debug_cmd && !strcmp(debug_cmd, "eglQuerySurface"), "debug callback called");
        eglDebugMessageControlKHR(NULL, NULL);
        eglGetError();
    }

    /* OpenGL ES */
    {
        EGLint e2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        EGLint e3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
        EGLContext es;
        const char *v;
        CHECK(eglBindAPI(EGL_OPENGL_ES_API), "bind OpenGL ES");
        CHECK(eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, e3) == EGL_NO_CONTEXT &&
              eglGetError() == EGL_BAD_MATCH, "ES 3.0 refused");
        es = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, e2);
        CHECK(es != EGL_NO_CONTEXT && eglMakeCurrent(dpy, pb, pb, es), "ES 2.0 context");
        v = (const char *) glGetString(GL_VERSION);
        say("ES: %s, GLSL %s\n", v ? v : "(null)", (const char *) glGetString(0x8B8C));
        CHECK(v && !strncmp(v, "OpenGL ES 2.0", 13), "ES 2.0 version string");
        eglMakeCurrent(dpy, pb, pb, ctx);
        eglDestroyContext(dpy, es);
        es = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
        CHECK(es != EGL_NO_CONTEXT && eglMakeCurrent(dpy, pb, pb, es), "ES 1.1 context");
        v = (const char *) glGetString(GL_VERSION);
        CHECK(v && !strncmp(v, "OpenGL ES-CM 1.1", 16), "ES 1.1 version string");
        eglMakeCurrent(dpy, pb, pb, ctx);
        eglDestroyContext(dpy, es);
        eglBindAPI(EGL_OPENGL_API);
    }

    /* error cases */
    CHECK(!eglBindAPI(EGL_OPENVG_API) && eglGetError() == EGL_BAD_PARAMETER, "OpenVG refused");
    {
        EGLint a[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
                       EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE };
        CHECK(eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, a) == EGL_NO_CONTEXT &&
              eglGetError() == EGL_BAD_MATCH, "GL 3.3 core refused");
    }
    CHECK(eglCreateWindowSurface(dpy, cfg, 0x12345678, NULL) == EGL_NO_SURFACE &&
          eglGetError() == EGL_BAD_NATIVE_WINDOW, "bogus window handle refused");
    CHECK(eglGetProcAddress("glGenBuffers") != NULL, "eglGetProcAddress(glGenBuffers)");
    CHECK(eglGetProcAddress("eglRedrawWindowRISCOS") != NULL, "eglGetProcAddress(eglRedrawWindowRISCOS)");

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, pb);
    eglDestroyContext(dpy, ctx);
    CHECK(eglTerminate(dpy), "terminate");
    say("%d checks, %d failed: %s\n", checks, failures, failures ? "FAIL" : "PASS");
    free(area);
    return failures != 0;
}

/* ------------------------------------------------------------------ */
/* Desktop window mode                                                 */

static int task_handle;

/* Why Wimp_Initialise failed: in a TaskWindow the program already is a
   Wimp task (the TaskWindow's), so it has to be started with *WimpTask. */
static const char *no_desktop_reason(void)
{
    _kernel_swi_regs r;
    r.r[0] = 0;
    if (_kernel_swi(0x43380 /* TaskWindow_TaskInfo */, &r, &r) == NULL && r.r[0] != 0)
        return "Can't open a window from a TaskWindow: use *WimpTask Run egltest ...";
    return "Needs the desktop (Wimp_Initialise failed)";
}

static int wimp_start(const char *name)
{
    static const int messages[] = { 0 };
    _kernel_swi_regs r;
    r.r[0] = 380;
    r.r[1] = TASK;
    r.r[2] = (int) name;
    r.r[3] = (int) messages;
    if (_kernel_swi(Wimp_Initialise, &r, &r) != NULL)
        return 0;
    task_handle = r.r[1];
    return 1;
}

static void wimp_end(void)
{
    _kernel_swi_regs r;
    if (!task_handle) return;
    r.r[0] = task_handle;
    r.r[1] = TASK;
    _kernel_swi(Wimp_CloseDown, &r, &r);
    task_handle = 0;
}

static int fx_x = 32, fx_y = -32, fx_w = 160, fx_h = 120;  /* -F x,y,w,h */
static int fx_appcopy, fx_first;                             /* -A, -O */
static int damage_demo;                                       /* -D */
static int keep_busy;                                         /* -k */
static int *fx_area, *fx_spr;
static int fx_errors;
static EGLint fx_last_error;
static EGLContext fx_ctx;

/* Diagnostic for the work area surface: what GL thinks it drew, and what
   is actually in the surface's memory (copied out with eglCopyBuffers into
   a sprite, also saved as Sprite file "eglfx"). */
static int *make_sprite_area(int w, int h, int **sprite);
static void fx_check(EGLSurface fx)
{
    unsigned char px[4] = { 0, 0, 0, 0 };
    GLint vp[4] = { 0, 0, 0, 0 };
    int *area, *spr;
    glFinish();
    glReadPixels(fx_w / 2, fx_h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGetIntegerv(GL_VIEWPORT, vp);
    say("second surface check: GL error 0x%x, viewport %d,%d %dx%d, GL reads R%02x G%02x B%02x",
        glGetError(), vp[0], vp[1], vp[2], vp[3], px[0], px[1], px[2]);
    area = make_sprite_area(fx_w, fx_h, &spr);
    if (area) {
        _kernel_swi_regs r;
        unsigned int *pix = (unsigned int *) ((char *) spr + spr[8]);
        if (eglCopyBuffers(dpy, fx, spr))
            say(", surface memory %08x (0x00BBGGRR)\n", pix[(fx_h - 1 - fx_h / 2) * fx_w + fx_w / 2]);
        else
            say(", eglCopyBuffers failed 0x%04x\n", eglGetError());
        r.r[0] = 256 + 12;
        r.r[1] = (int) area;
        r.r[2] = (int) "eglfx";
        _kernel_swi(OS_SpriteOp, &r, &r);
        free(area);
    } else {
        say("\n");
    }
}

/* Scroll wheel zoom (as in sdlgltest): OS_Pointer 2 gives the wheel's
   accumulated position; only used while the pointer is over our window,
   and not with -r, where the wheel scrolls the window. */
static void wheel_zoom(int handle, int second)
{
    static int last_y, valid;
    int ptr[5];
    _kernel_swi_regs r;
    int dy;

    r.r[0] = 2;
    if (_kernel_swi(OS_Pointer, &r, &r) != NULL)
        return;
    dy = r.r[1] - last_y;
    last_y = r.r[1];
    if (!valid) { valid = 1; return; }
    if (dy == 0 || dy > 64 || dy < -64 || second)
        return;
    r.r[1] = (int) ptr;
    if (_kernel_swi(Wimp_GetPointerInfo, &r, &r) != NULL || ptr[3] != handle)
        return;
    zoom -= 0.5f * dy;
    if (zoom < 3.5f) zoom = 3.5f;
    if (zoom > 20) zoom = 20;
}

/* -A: copy the second surface into the test program's own sprite and plot
   that over the same place, bypassing the library's plot. */
static void app_plot_copy(int handle, EGLSurface fx)
{
    int b[16], more;
    _kernel_swi_regs r;
    if (!eglCopyBuffers(dpy, fx, fx_spr))
        return;
    b[0] = handle;
    b[1] = fx_x; b[2] = fx_y - fx_h * 2; b[3] = fx_x + fx_w * 2; b[4] = fx_y;
    r.r[1] = (int) b;
    if (_kernel_swi(Wimp_UpdateWindow, &r, &r) != NULL)
        return;
    more = r.r[0];
    while (more) {
        r.r[0] = 512 + 34;
        r.r[1] = (int) fx_area;
        r.r[2] = (int) fx_spr;
        r.r[3] = b[1] - b[5] + fx_x;
        r.r[4] = b[4] - b[6] + fx_y - fx_h * 2;
        r.r[5] = 0;
        _kernel_swi(OS_SpriteOp, &r, &r);
        r.r[1] = (int) b;
        if (_kernel_swi(Wimp_GetRectangle, &r, &r) != NULL)
            break;
        more = r.r[0];
    }
}

static int rs_w, rs_h;                            /* -S WxH: render size */
static int overlay_opt;                           /* -V 1, -n -1, neither 0 */

/* EGL_OVERLAY_RISCOS: 0 plotted, 1 shown through the overlay, 2 hidden */
static int overlay_state(EGLSurface s)
{
    EGLint v = 0;
    eglQuerySurface(dpy, s, EGL_OVERLAY_RISCOS, &v);
    return v;
}

static int run_window(int second, double limit)
{
    static char title[128] = "egltest";
    static const char *const ovl_names[] = { "plotted", "overlay", "overlay hidden" };
    double ovl_present[3] = { 0, 0, 0 };
    long ovl_frames[3] = { 0, 0, 0 };
    const char *ev = getenv("EGL$Overlay");
    int ovl_want = overlay_opt > 0 || (overlay_opt == 0 && ev &&
                   (!strcmp(ev, "on") || !strcmp(ev, "On") || !strcmp(ev, "yes") || !strcmp(ev, "1")));
    int paused = 0, toggles = 0, ovl_now = 0, ovl_changes = 0;
    int wb[23], block[64], handle;
    _kernel_swi_regs r;
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface ws = EGL_NO_SURFACE, fx = EGL_NO_SURFACE;
    EGLint w = 0, h = 0;
    double t0, t1, t2, tstart, last_title, render = 0, present = 0;
    long frames = 0, title_frames = 0;
    float a = 0;
    int quit = 0;

    collect = 1;
    if (!wimp_start("egltest")) { collect = 0; say("%s\n", no_desktop_reason()); return 1; }
    if (!egl_start()) { wimp_end(); collect = 0; fputs(summary, stdout); return 1; }

    memset(wb, 0, sizeof wb);
    wb[0] = 200; wb[1] = 200; wb[2] = 200 + 1280; wb[3] = 200 + 960;   /* 640x480 at eig 1 */
    wb[4] = 0; wb[5] = 0; wb[6] = -1;
    /* Moveable, back, close, title, toggle size, adjust size. Scroll bars
       only with -r: the main surface is pinned to the visible area, so
       without a work area surface there's nothing to scroll, and the wheel
       zooms instead. */
    wb[7] = (int) (second ? 0xFF000002u : 0xCF000002u);
    wb[8] = 7 | (2 << 8) | (7 << 16) | (4 << 24);   /* title fg/bg, work fg/bg (grey) */
    wb[9] = 3 | (1 << 8) | (12 << 16);
    wb[10] = 0; wb[11] = -2400; wb[12] = 3840; wb[13] = 0;
    wb[14] = 0x07000119;                    /* title: text, centred, indirected */
    wb[15] = 3 << 12;                       /* clicks (to give it the caret: H, P keys) */
    wb[16] = 1;
    wb[17] = 0;
    wb[18] = (int) title; wb[19] = -1; wb[20] = sizeof title;
    wb[21] = 0;
    r.r[1] = (int) wb;
    if (_kernel_swi(Wimp_CreateWindow, &r, &r) != NULL) { wimp_end(); return 1; }
    handle = r.r[0];
    block[0] = handle;
    memcpy(&block[1], wb, 7 * sizeof(int));
    r.r[1] = (int) block;
    _kernel_swi(Wimp_OpenWindow, &r, &r);

    cfg = pick_config(EGL_WINDOW_BIT, 16);
    ctx = cfg ? eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL) : EGL_NO_CONTEXT;
    {
        EGLint oa[8];
        int n = 0;
        if (overlay_opt) {
            oa[n++] = EGL_OVERLAY_RISCOS; oa[n++] = overlay_opt > 0 ? EGL_TRUE : EGL_FALSE;
        }
        if (rs_w > 0) {
            oa[n++] = EGL_RENDER_WIDTH_RISCOS; oa[n++] = rs_w;
            oa[n++] = EGL_RENDER_HEIGHT_RISCOS; oa[n++] = rs_h;
        }
        oa[n] = EGL_NONE;
        if (!fx_first)
            ws = cfg ? eglCreateWindowSurface(dpy, cfg, handle, oa) : EGL_NO_SURFACE;
    }
    fx_ctx = ctx;
    if (second == 2 && ctx != EGL_NO_CONTEXT)
        fx_ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);   /* -R: its own context */
    if (second && cfg) {
        EGLint fa[] = { EGL_WORK_AREA_X_RISCOS, fx_x, EGL_WORK_AREA_Y_RISCOS, fx_y,
                        EGL_WORK_AREA_WIDTH_RISCOS, fx_w, EGL_WORK_AREA_HEIGHT_RISCOS, fx_h, EGL_NONE };
        fx = eglCreateWindowSurface(dpy, cfg, handle, fa);
        if (fx == EGL_NO_SURFACE) say("work area surface failed (0x%04x)\n", eglGetError());
        say("second surface at work area %d,%d, %dx%d%s%s\n", fx_x, fx_y, fx_w, fx_h,
            fx_first ? ", created first" : "", fx_appcopy ? ", test program plots its own copy" : "");
        if (fx_appcopy)
            fx_area = make_sprite_area(fx_w, fx_h, &fx_spr);
    }
    if (fx_first) {
        ws = cfg ? eglCreateWindowSurface(dpy, cfg, handle, NULL) : EGL_NO_SURFACE;
    }
    if (ctx == EGL_NO_CONTEXT || ws == EGL_NO_SURFACE || !eglMakeCurrent(dpy, ws, ws, ctx)) {
        say("EGL window setup failed (0x%04x)\n", eglGetError());
        wimp_end();
        collect = 0;
        fputs(summary, stdout);
        return 1;
    }
    say("GL_RENDERER %s, GL_VERSION %s\n", (const char *) glGetString(GL_RENDERER),
        (const char *) glGetString(GL_VERSION));
    {
        _kernel_swi_regs m;
        m.r[1] = (int) "VideoOverlay_Create";
        say("EGL_RISCOS_overlay %s; VideoOverlay %s; overlay %s\n",
            strstr(eglQueryString(dpy, EGL_EXTENSIONS), "EGL_RISCOS_overlay") ? "listed" : "NOT listed",
            _kernel_swi(OS_SWINumberFromString, &m, &m) == NULL ? "loaded" : "not loaded",
            overlay_opt > 0 ? "asked for (-V)" : overlay_opt < 0 ? "refused (-n)" :
            ovl_want ? "on (EGL$Overlay)" : "not asked for");
    }

    tstart = last_title = hr_seconds();
    while (!quit) {
        r.r[0] = 0;                         /* null events on: animate */
        r.r[1] = (int) block;
        if (_kernel_swi(Wimp_Poll, &r, &r) != NULL) break;
        switch (r.r[0]) {
        case 0:                             /* null: draw a frame */
            if (paused) {                   /* a paused video: keep the overlay right */
                eglCheckOverlaysRISCOS(dpy);
                if (overlay_state(ws) != ovl_now) {
                    ovl_now = overlay_state(ws);
                    ovl_changes++;
                    snprintf(title, sizeof title, "egltest paused  %s", ovl_names[ovl_now]);
                    r.r[0] = handle; r.r[1] = TASK; r.r[2] = 3;
                    _kernel_swi(Wimp_ForceRedraw, &r, &r);
                }
                if (limit > 0 && hr_seconds() - tstart >= limit) quit = 1;
                break;
            }
            eglQuerySurface(dpy, ws, EGL_WIDTH, &w);
            eglQuerySurface(dpy, ws, EGL_HEIGHT, &h);
            t0 = hr_seconds();
            if (damage_demo) {
                /* Buffer age 1 = the buffer still holds the last frame:
                   redraw and show only the middle. */
                EGLint age = 0, rect[4];
                /* Background fades blue -> red -> blue over 8 s, by
                   the clock: counting frames made it flash at 500 fps. */
                double ph = (t0 - tstart) / 8.0;
                float c = (float) (ph - (long) ph);
                c = c < 0.5f ? 2 * c : 2 - 2 * c;
                eglQuerySurface(dpy, ws, EGL_BUFFER_AGE_EXT, &age);
                rect[0] = w / 4; rect[1] = h / 4; rect[2] = w / 2; rect[3] = h / 2;
                if (age == 1) {
                    glEnable(GL_SCISSOR_TEST);
                    glScissor(rect[0], rect[1], rect[2], rect[3]);
                }
                scene(w, h, a, 0.1f + 0.4f * c, 0.1f, 0.45f - 0.4f * c);
                glDisable(GL_SCISSOR_TEST);
                glFinish();
                t1 = hr_seconds();
                if (age == 1)
                    eglSwapBuffersWithDamageKHR(dpy, ws, rect, 1);
                else
                    eglSwapBuffers(dpy, ws);
            } else {
                scene(w, h, a, 0.1f, 0.1f, 0.25f);
                glFinish();
                t1 = hr_seconds();
                eglSwapBuffers(dpy, ws);
            }
            if (fx != EGL_NO_SURFACE) {
                float z = zoom;
                zoom = 6;
                if (!eglMakeCurrent(dpy, fx, fx, fx_ctx)) {
                    fx_errors++;
                    fx_last_error = eglGetError();
                } else {
                    scene(fx_w, fx_h, -2 * a, 0.3f, 0.1f, 0.1f);
                    if (frames == 5)
                        fx_check(fx);
                    if (!eglSwapBuffers(dpy, fx)) {
                        fx_errors++;
                        fx_last_error = eglGetError();
                    }
                    if (fx_area)
                        app_plot_copy(handle, fx);
                }
                zoom = z;
                eglMakeCurrent(dpy, ws, ws, ctx);
            }
            wheel_zoom(handle, second);
            t2 = hr_seconds();
            {
                int st = overlay_state(ws);
                if (st != ovl_now) ovl_changes++;
                ovl_now = st;
                ovl_present[st] += t2 - t1;
                ovl_frames[st]++;
            }
            render += t1 - t0;
            present += t2 - t1;
            frames++;
            title_frames++;
            a += 2;
            if (t2 - last_title >= 1.0) {
                snprintf(title, sizeof title, "egltest %dx%d  %.1f fps  render %.1f ms  present %.1f ms  %s",
                         w, h, title_frames / (t2 - last_title),
                         1000 * render / frames, 1000 * present / frames, ovl_names[ovl_now]);
                r.r[0] = handle; r.r[1] = TASK; r.r[2] = 3;    /* redraw title bar */
                _kernel_swi(Wimp_ForceRedraw, &r, &r);
                title_frames = 0;
                last_title = t2;
            }
            if (limit > 0 && t2 - tstart >= limit) quit = 1;
            break;
        case 1:                             /* redraw request */
            if (!eglRedrawWindowRISCOS(dpy, block)) {
                r.r[1] = (int) block;       /* not ours: empty redraw loop */
                _kernel_swi(Wimp_RedrawWindow, &r, &r);
                while (r.r[0]) { r.r[1] = (int) block; _kernel_swi(Wimp_GetRectangle, &r, &r); }
            }
            break;
        case 2:                             /* open request */
            r.r[1] = (int) block;
            _kernel_swi(Wimp_OpenWindow, &r, &r);
            break;
        case 3:                             /* close */
            quit = 1;
            break;
        case 6:                             /* click: take the caret for H and P */
            r.r[0] = handle; r.r[1] = -1; r.r[2] = 0; r.r[3] = 0;
            r.r[4] = (1 << 25) | 40; r.r[5] = -1;          /* invisible */
            _kernel_swi(Wimp_SetCaretPosition, &r, &r);
            break;
        case 8:                             /* key */
            if (block[6] == 'h' || block[6] == 'H') {
                ovl_want = !ovl_want;
                toggles++;
                eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, ovl_want ? EGL_TRUE : EGL_FALSE);
                say("H: overlay %s\n", ovl_want ? "on" : "off");
            } else if (block[6] == 's' || block[6] == 'S') {
                EGLint cur = 0;
                eglQuerySurface(dpy, ws, EGL_RENDER_WIDTH_RISCOS, &cur);
                if (cur) {
                    eglSurfaceAttrib(dpy, ws, EGL_RENDER_WIDTH_RISCOS, 0);
                    say("S: render at the window's size\n");
                } else {
                    int sw2 = rs_w > 0 ? rs_w : w / 2, sh2 = rs_h > 0 ? rs_h : h / 2;
                    eglSurfaceAttrib(dpy, ws, EGL_RENDER_WIDTH_RISCOS, sw2 > 0 ? sw2 : 1);
                    eglSurfaceAttrib(dpy, ws, EGL_RENDER_HEIGHT_RISCOS, sh2 > 0 ? sh2 : 1);
                    say("S: render at %dx%d, scaled\n", sw2, sh2);
                }
            } else if (block[6] == 'p' || block[6] == 'P') {
                paused = !paused;
                say("P: %s\n", paused ? "paused" : "running");
                if (paused) {
                    snprintf(title, sizeof title, "egltest paused  %s", ovl_names[overlay_state(ws)]);
                    r.r[0] = handle; r.r[1] = TASK; r.r[2] = 3;
                    _kernel_swi(Wimp_ForceRedraw, &r, &r);
                }
            } else {
                r.r[0] = block[6];
                _kernel_swi(Wimp_ProcessKey, &r, &r);
            }
            break;
        case 17: case 18:
            if (block[4] == 0) quit = 1;    /* Message_Quit */
            break;
        }
    }
    {
        double t = hr_seconds() - tstart;
        int i;
        say("window %dx%d: %ld frames in %.1f s = %.1f fps; render %.2f ms, present %.2f ms per frame%s\n",
            w, h, frames, t, frames / (t > 0 ? t : 1), frames ? 1000 * render / frames : 0,
            frames ? 1000 * present / frames : 0, fx != EGL_NO_SURFACE ? " (incl. the second surface)" : "");
        for (i = 0; i < 3; i++)
            if (ovl_frames[i])
                say("  %s: %ld frames, present %.2f ms per frame\n", ovl_names[i], ovl_frames[i],
                    1000 * ovl_present[i] / ovl_frames[i]);
        say("overlay state changes %d, H pressed %d times\n", ovl_changes, toggles);
        if (fx != EGL_NO_SURFACE)
            say("second surface: %d errors%s (last EGL error 0x%04x)\n", fx_errors,
                fx_errors ? " - it wasn't drawn" : "", fx_last_error);
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    wimp_end();
    collect = 0;
    fputs(summary, stdout);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Full screen mode                                                    */

static int want_banks, pattern;

static int run_fullscreen(int direct, int interval, double limit)
{
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface fs;
    EGLint w, h, rb, banks = 0;
    char how[64];
    EGLint attrs[] = { EGL_RENDER_BUFFER, direct ? EGL_SINGLE_BUFFER : EGL_BACK_BUFFER,
                       EGL_SCREEN_BANKS_RISCOS, want_banks, EGL_NONE, 0, EGL_NONE, 0, EGL_NONE };
    int bar = 0;
    double t0, t1, t2, tr, tstart, render = 0, present = 0, freed = 0;
    long frames = 0;
    float a = 0;
    int in_desktop;

    if (rs_w > 0) {
        attrs[4] = EGL_RENDER_WIDTH_RISCOS; attrs[5] = rs_w;
        attrs[6] = EGL_RENDER_HEIGHT_RISCOS; attrs[7] = rs_h;
    }
    collect = 1;
    in_desktop = wimp_start("egltest full screen");
    if (!egl_start()) { collect = 0; fputs(summary, stdout); return 1; }
    cfg = pick_config(EGL_WINDOW_BIT, 16);
    ctx = cfg ? eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL) : EGL_NO_CONTEXT;
    fs = cfg ? eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, attrs) : EGL_NO_SURFACE;
    if (ctx == EGL_NO_CONTEXT || fs == EGL_NO_SURFACE || !eglMakeCurrent(dpy, fs, fs, ctx)) {
        say("EGL full screen setup failed (0x%04x)\n", eglGetError());
        wimp_end();
        collect = 0;
        fputs(summary, stdout);
        return 1;
    }
    eglSwapInterval(dpy, interval);
    eglQuerySurface(dpy, fs, EGL_WIDTH, &w);
    eglQuerySurface(dpy, fs, EGL_HEIGHT, &h);
    eglQuerySurface(dpy, fs, EGL_RENDER_BUFFER, &rb);
    eglQuerySurface(dpy, fs, EGL_SCREEN_BANKS_RISCOS, &banks);

    tstart = hr_seconds();
    do {
        t0 = hr_seconds();
        if (pattern) {
            /* -p: a white bar sweeping across black. A tear shows as the
               bar broken sideways at the tear line. */
            glViewport(0, 0, w, h);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);
            glScissor(bar, 0, 48, h);
            glClearColor(1, 1, 1, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
            bar = (bar + 24) % (w > 48 ? w - 48 : 1);
        } else {
            scene(w, h, a, 0.1f, 0.25f, 0.1f);
        }
        glFinish();
        tr = hr_seconds();
        if (keep_busy) {
            /* -k: other work in 0.5 ms pieces while a swap would block */
            double w0 = tr, w;
            while (eglSwapWouldWaitRISCOS(dpy, fs)) {
                w = hr_seconds();
                while (hr_seconds() - w < 0.0005)
                    ;
                if (hr_seconds() - w0 > 0.1)
                    break;                  /* (never more than 100 ms) */
            }
            freed += hr_seconds() - w0;
        }
        t1 = hr_seconds();
        eglSwapBuffers(dpy, fs);
        t2 = hr_seconds();
        render += tr - t0;                  /* (not the -k work) */
        present += t2 - t1;
        frames++;
        a += 2;
    } while (t2 - tstart < limit);

    if (rs_w > 0)
        snprintf(how, sizeof how, "scaled to the screen (sprite plot)");
    else if (rb == EGL_SINGLE_BUFFER)
        snprintf(how, sizeof how, "direct to screen memory");
    else if (banks > 0)
        snprintf(how, sizeof how, "%d screen banks", banks);
    else
        snprintf(how, sizeof how, "double buffered (sprite plot)");
    say("full screen %dx%d, %s, swap interval %d: %ld frames in %.1f s = %.1f fps; "
        "render %.2f ms, present %.2f ms per frame\n",
        w, h, how,
        interval, frames, t2 - tstart, frames / (t2 - tstart), 1000 * render / frames,
        1000 * present / frames);
    if (keep_busy)
        say("  -k: %.2f ms per frame freed for other work while a swap would have waited "
            "(present above is the time still blocked in the swap, plus the plot)\n",
            1000 * freed / frames);
    if (want_banks && banks == 0)
        say("  (no screen banks: not enough screen memory, or not a 32bpp mode in this colour order)\n");
    if (direct && rb != EGL_SINGLE_BUFFER)
        say("  (-d asked for direct rendering but the screen mode isn't 32bpp in this colour order)\n");
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    if (in_desktop) {
        _kernel_swi_regs r;
        r.r[0] = -1; r.r[1] = 0; r.r[2] = 0; r.r[3] = 0x7FFF; r.r[4] = 0x7FFF;
        _kernel_swi(Wimp_ForceRedraw, &r, &r);  /* repaint the desktop */
        wimp_end();
    }
    collect = 0;
    fputs(summary, stdout);
    return 0;
}

int main(int argc, char **argv)
{
    int mode = 'c', second = 0, direct = 0, interval = 1, i, rc;
    double limit = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w")) mode = 'w';
        else if (!strcmp(argv[i], "-f")) mode = 'f';
        else if (!strcmp(argv[i], "-r")) second = 1;
        else if (!strcmp(argv[i], "-R")) second = 2;
        else if (!strcmp(argv[i], "-A")) fx_appcopy = 1;
        else if (!strcmp(argv[i], "-O")) fx_first = 1;
        else if (!strcmp(argv[i], "-D")) damage_demo = 1;
        else if (!strcmp(argv[i], "-k")) keep_busy = 1;
        else if (!strcmp(argv[i], "-n")) overlay_opt = -1;
        else if (!strcmp(argv[i], "-V")) overlay_opt = 1;
        else if (!strcmp(argv[i], "-S") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &rs_w, &rs_h) != 2 || rs_w < 1 || rs_h < 1) rs_w = rs_h = 0;
        }
        else if (!strcmp(argv[i], "-F") && i + 1 < argc)
            sscanf(argv[++i], "%d,%d,%d,%d", &fx_x, &fx_y, &fx_w, &fx_h);
        else if (!strcmp(argv[i], "-d")) direct = 1;
        else if (!strcmp(argv[i], "-p")) pattern = 1;
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) want_banks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) limit = atof(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            outf = fopen(argv[++i], "w");
            if (!outf) printf("can't write %s\n", argv[i]);
        } else {
            printf("usage: egltest [-o file] | -w [-r|-R] [-D] [-V|-n] [-S WxH] [-t secs] [-o file] | "
                   "-f [-d] [-b n] [-v n] [-p] [-S WxH] [-t secs] [-o file]\n");
            return 1;
        }
    }
    if (mode == 'w') rc = run_window(second, limit);
    else if (mode == 'f') rc = run_fullscreen(direct, interval, limit > 0 ? limit : 5);
    else rc = run_checks();
    if (outf) fclose(outf);
    return rc;
}
