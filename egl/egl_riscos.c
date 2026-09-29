/*
 * egl_riscos.c - EGL 1.4 for RISC OS on top of Mesa's OSMesa (software GL).
 * Part of riscos-mesa. MIT licence (see LICENSE).
 *
 * One display (the screen). Configs: RGBA8888 with depth 0/16/24 and
 * stencil 0/8, in both RISC OS 32bpp colour orders (the one matching the
 * screen first). Client API: desktop OpenGL (2.1 compatibility, from Mesa
 * 20.3 classic OSMesa). Surfaces:
 *   window  - Wimp window handle, or -1 for the whole screen. Rendered into
 *             a sprite that eglSwapBuffers plots (Wimp_UpdateWindow loop in a
 *             window, a plain plot full screen), or straight into screen
 *             memory full screen with EGL_RENDER_BUFFER = EGL_SINGLE_BUFFER
 *             or screen banks. Optionally at a fixed render size stretched
 *             to fit, and in a window shown through a hardware overlay
 *             (EGL_RISCOS_overlay). libbcm_host's DispmanX elements are
 *             window surfaces too.
 *   pbuffer - plain memory.
 *   pixmap  - a 32bpp sprite; GL renders into its image directly.
 * Images (EGL_KHR_image_pixmap): a 32bpp sprite, used in place as a GL
 * texture by glEGLImageTargetTexture2DOES (GL_OES_EGL_image, from our
 * Mesa patch), so a program can write each new frame into the sprite.
 * Extensions: see EGL_RISCOS_EXTENSIONS / EGL_RISCOS_CLIENT_EXTENSIONS
 * below and the table in README.md.
 * See include/EGL/eglext_riscos.h and README.md for the RISC OS details.
 *
 * Source layout: egl_internal.h has what the parts share (limits, SWI
 * numbers, the object types, the lock, and the prototypes of the helpers
 * one part uses from another). This file has the globals, the lock and
 * error reporting, and includes the rest, which is in egl/parts/ by topic:
 *   screen.c      the screen, Wimp windows, plotting frames (swap/present)
 *   buffers.c     sprites, screen banks, pbuffer memory, pixmaps
 *   overlay.c     window surfaces shown through a hardware overlay
 *   validation.c  checking handles, unlinking destroyed objects
 *   configs.c     the configs and eglChooseConfig matching
 *   current.c     binding contexts and surfaces to OSMesa
 *                 (eglMakeCurrent's work)
 *   api.c         the EGL 1.4 entry points
 *   extensions.c  partial update, lock surface, sync objects, images,
 *                 platform, debug, EGL_RISCOS_wimp_window, eglGetProcAddress
 * They are one compilation unit (build only this file), so the helpers
 * shared between them stay static and invisible to programs linking
 * libEGL.a.
 *
 * Threads: the error code, the bound API and the current contexts are kept
 * for each thread, as EGL requires, and each EGL call holds one lock, so
 * threads can use EGL and GL at once (each with its own current context).
 * A thread can have an OpenGL and an OpenGL ES context current together;
 * GL calls go to the bound API's one (see parts/current.c).
 *
 * Every surface has its own OSMesaBuffer (patches/mesa riscos-osmesa-
 * buffers): the colour is the surface's memory, and the depth and stencil
 * buffers belong to the surface, not to a context, so contexts can share a
 * surface, and draw into one surface while reading from another.
 */
#include "egl_internal.h"

static egl_display display = { MAGIC_DISPLAY, 0, {{0, 0, 0, 0}}, 0, NULL, NULL, NULL, NULL, NULL };

static pthread_key_t thread_key;
static pthread_mutex_t egl_mutex;
static egl_thread first_thread; /* if a thread's state can't be allocated */


/* A thread that ends with contexts current releases them (as
   eglReleaseThread), so other threads can use them and their surfaces. */
static void thread_exit(void *p)
{
    egl_thread *t = (egl_thread *) p;
    pthread_mutex_lock(&egl_mutex);
    release_slot(&display, t, 0);
    release_slot(&display, t, 1);
    pthread_mutex_unlock(&egl_mutex);
    if (t != &first_thread)
        free(t);
}

static void egl_once_init(void)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&egl_mutex, &a);
    pthread_mutexattr_destroy(&a);
    pthread_key_create(&thread_key, thread_exit);
}

static pthread_once_t egl_once = PTHREAD_ONCE_INIT;

/* The calling thread's state */
static egl_thread *thr(void)
{
    egl_thread *t;
    pthread_once(&egl_once, egl_once_init);
    t = (egl_thread *) pthread_getspecific(thread_key);
    if (!t) {
        t = (egl_thread *) calloc(1, sizeof *t);
        if (!t)
            t = &first_thread;
        /* The EGL spec's initial API is OpenGL ES when it's supported (code
           written for the Pi's Khronos stack relies on it); desktop GL code
           binds EGL_OPENGL_API. */
        t->error = EGL_SUCCESS;
        t->api = EGL_OPENGL_ES_API;
        pthread_setspecific(thread_key, t);
    }
    return t;
}

/* EGL_KHR_debug: every public function records its name on entry, and the
   validation helpers record the label of the object they found (both
   under the lock). */
static const char *egl_cmd = "";
static EGLLabelKHR egl_obj_label;
static EGLDEBUGPROCKHR debug_callback;
static int debug_enabled[4] = { 1, 1, 0, 0 };   /* critical, error, warn, info */

/* Every public function starts with ENTER(): it takes the lock, which is
   released when the function returns (GCC's cleanup attribute), and
   records the function's name for EGL_KHR_debug. */
static int egl_enter(const char *cmd)
{
    pthread_once(&egl_once, egl_once_init);
    pthread_mutex_lock(&egl_mutex);
    egl_cmd = cmd;
    egl_obj_label = NULL;
    return 1;
}

static void egl_leave(int *entered)
{
    (void) entered;
    pthread_mutex_unlock(&egl_mutex);
}


/* The lock alone, for code that Mesa calls rather than the program (so it
   isn't an EGL command for EGL_KHR_debug). Released on return, as ENTER(). */
static int egl_lock(void)
{
    pthread_once(&egl_once, egl_once_init);
    pthread_mutex_lock(&egl_mutex);
    return 1;
}


static const char *error_name(EGLint e)
{
    static const char *names[] = {
        "EGL_SUCCESS", "EGL_NOT_INITIALIZED", "EGL_BAD_ACCESS", "EGL_BAD_ALLOC",
        "EGL_BAD_ATTRIBUTE", "EGL_BAD_CONFIG", "EGL_BAD_CONTEXT",
        "EGL_BAD_CURRENT_SURFACE", "EGL_BAD_DISPLAY", "EGL_BAD_MATCH",
        "EGL_BAD_NATIVE_PIXMAP", "EGL_BAD_NATIVE_WINDOW", "EGL_BAD_PARAMETER",
        "EGL_BAD_SURFACE", "EGL_CONTEXT_LOST"
    };
    if (e >= EGL_SUCCESS && e <= EGL_CONTEXT_LOST)
        return names[e - EGL_SUCCESS];
    return "EGL error";
}

static EGLBoolean fail(EGLint error)
{
    egl_thread *t = thr();
    t->error = error;
    if (debug_callback && debug_enabled[error == EGL_BAD_ALLOC ? 0 : 1])
        debug_callback(error, egl_cmd,
                       error == EGL_BAD_ALLOC ? EGL_DEBUG_MSG_CRITICAL_KHR : EGL_DEBUG_MSG_ERROR_KHR,
                       t->label, egl_obj_label, error_name(error));
    return EGL_FALSE;
}

static EGLBoolean ok(void)
{
    thr()->error = EGL_SUCCESS;
    return EGL_TRUE;
}

/* ------------------------------------------------------------------ */
/* The rest of the library, one unit with this file (see the list in   */
/* the comment at the top).                                            */

#include "parts/screen.c"
#include "parts/buffers.c"
#include "parts/overlay.c"
#include "parts/validation.c"
#include "parts/configs.c"
#include "parts/current.c"
#include "parts/api.c"
#include "parts/extensions.c"
