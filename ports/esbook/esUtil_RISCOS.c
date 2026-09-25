/*
 * esUtil_RISCOS.c - RISC OS version of the window and main loop part of
 * the "OpenGL ES 2.0 Programming Guide" samples' framework (esUtil). It
 * replaces LinuxX11/Common/esUtil.c; the samples and the book's
 * esShader.c, esShapes.c and esTransform.c build unchanged against it.
 *
 * It implements esUtil.h's window, loop, callback, logging and TGA
 * functions with riscos-mesa's native EGL: the window is a Wimp window
 * and its handle is the EGL native window.
 *
 * Differences from the X11 version:
 *  - ES_WINDOW_MULTISAMPLE is ignored (riscos-mesa has no multisample
 *    configs); the sample runs without antialiasing.
 *  - Escape or the window's close icon quits. Other keys go to the key
 *    callback (characters only, as in the X11 version); unused keys are
 *    passed back to the Wimp.
 *  - If ESUtil$Output is set (the !Run file sets it), text output goes
 *    to that file and the newest line (the FPS report) is shown in the
 *    title bar; printing from a Wimp task would open a command window.
 *  - esLoadTGA also looks in the directory named by ESUtil$Dir, so a
 *    sample finds its .tga files inside its application directory.
 *
 * Written for riscos-mesa (not derived from the book's esUtil.c).
 * MIT licence (see LICENCES.txt).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "esUtil.h"
#include "riscos_wimpwin.h"

static rw_window rwin;
static char base_title[96];

static void finish(void)
{
    rw_close(&rwin);
    rw_end();
}

void ESUTIL_API esInitContext(ESContext *esContext)
{
    if (esContext)
        memset(esContext, 0, sizeof *esContext);
}

GLboolean ESUTIL_API esCreateWindow(ESContext *esContext, const char *title,
                                    GLint width, GLint height, GLuint flags)
{
    EGLint attribs[] = {
        EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5,
        EGL_ALPHA_SIZE,   (flags & ES_WINDOW_ALPHA) ? 8 : 0,
        EGL_DEPTH_SIZE,   (flags & ES_WINDOW_DEPTH) ? 8 : 0,
        EGL_STENCIL_SIZE, (flags & ES_WINDOW_STENCIL) ? 8 : 0,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    static const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLConfig config;
    EGLint n = 0;

    if (!esContext)
        return GL_FALSE;
    rw_redirect_output("ESUtil$Output");
    if (flags & ES_WINDOW_MULTISAMPLE)
        esLogMessage("esCreateWindow: no multisampling on riscos-mesa, using none\n");
    if (!rw_init(title))
        return GL_FALSE;
    atexit(finish);
    snprintf(base_title, sizeof base_title, "%s", title);
    if (!rw_open(&rwin, title, -1, -1, width, height))
        return GL_FALSE;
    esContext->width = width;
    esContext->height = height;
    esContext->hWnd = (EGLNativeWindowType) rwin.handle;

    esContext->eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (esContext->eglDisplay == EGL_NO_DISPLAY ||
        !eglInitialize(esContext->eglDisplay, NULL, NULL) ||
        !eglChooseConfig(esContext->eglDisplay, attribs, &config, 1, &n) || n < 1)
        return GL_FALSE;
    eglBindAPI(EGL_OPENGL_ES_API);
    esContext->eglSurface = eglCreateWindowSurface(esContext->eglDisplay, config,
                                                   esContext->hWnd, NULL);
    esContext->eglContext = eglCreateContext(esContext->eglDisplay, config,
                                             EGL_NO_CONTEXT, ctx_attribs);
    if (esContext->eglSurface == EGL_NO_SURFACE || esContext->eglContext == EGL_NO_CONTEXT)
        return GL_FALSE;
    return eglMakeCurrent(esContext->eglDisplay, esContext->eglSurface,
                          esContext->eglSurface, esContext->eglContext) ? GL_TRUE : GL_FALSE;
}

void ESUTIL_API esMainLoop(ESContext *esContext)
{
    unsigned last = rw_millis(), report = last;
    unsigned frames = 0;

    for (;;) {
        int key = 0, ev = rw_poll(&rwin, esContext->eglDisplay, 1, &key);
        if (ev == RW_CLOSE)
            break;
        if (ev == RW_KEY) {
            if (key == RW_KEY_ESCAPE)
                break;
            if (key < 0x100 && esContext->keyFunc)
                esContext->keyFunc(esContext, (unsigned char) key, 0, 0);
            else
                rw_pass_key(key);
            continue;
        }
        if (ev != RW_IDLE)
            continue;
        {
            unsigned now = rw_millis();
            EGLint w = 0, h = 0;
            float dt = (now - last) / 1000.0f;
            last = now;
            if (esContext->updateFunc)
                esContext->updateFunc(esContext, dt);
            if (esContext->drawFunc)
                esContext->drawFunc(esContext);
            eglSwapBuffers(esContext->eglDisplay, esContext->eglSurface);
            /* The surface follows the window's visible area: the samples
               read esContext->width/height for glViewport each frame. */
            eglQuerySurface(esContext->eglDisplay, esContext->eglSurface, EGL_WIDTH, &w);
            eglQuerySurface(esContext->eglDisplay, esContext->eglSurface, EGL_HEIGHT, &h);
            if (w > 0 && h > 0) { esContext->width = w; esContext->height = h; }
            frames++;
            if (now - report >= 2000) {
                printf("%4u frames rendered in %1.4f seconds -> FPS=%3.4f\n",
                       frames, (now - report) / 1000.0f, frames * 1000.0f / (now - report));
                report = now;
                frames = 0;
            }
            rw_show_output(&rwin, base_title);
        }
    }
    eglMakeCurrent(esContext->eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(esContext->eglDisplay);
    finish();
}

void ESUTIL_API esRegisterDrawFunc(ESContext *esContext, void (ESCALLBACK *drawFunc)(ESContext *))
{
    esContext->drawFunc = drawFunc;
}

void ESUTIL_API esRegisterUpdateFunc(ESContext *esContext,
                                     void (ESCALLBACK *updateFunc)(ESContext *, float))
{
    esContext->updateFunc = updateFunc;
}

void ESUTIL_API esRegisterKeyFunc(ESContext *esContext,
                                  void (ESCALLBACK *keyFunc)(ESContext *, unsigned char, int, int))
{
    esContext->keyFunc = keyFunc;
}

void ESUTIL_API esLogMessage(const char *formatStr, ...)
{
    va_list ap;
    va_start(ap, formatStr);
    vprintf(formatStr, ap);
    va_end(ap);
}

static FILE *open_tga(const char *name)
{
    FILE *f = fopen(name, "rb");
    if (!f && getenv("ESUtil$Dir") && name[0] != '/') {
        char path[256];
        snprintf(path, sizeof path, "/<ESUtil$Dir>/%s", name);
        f = fopen(path, "rb");
    }
    return f;
}

/* Uncompressed 24 or 32 bit TGA, bottom-up rows as stored (as the book's
   loader returns them); the caller frees the buffer. */
char *ESUTIL_API esLoadTGA(char *fileName, int *width, int *height)
{
    unsigned char h[18];
    unsigned bytes;
    size_t size;
    char *buf;
    FILE *f = open_tga(fileName);

    if (!f)
        return NULL;
    if (fread(h, 1, sizeof h, f) != sizeof h || (h[2] != 2) || (h[16] != 24 && h[16] != 32)) {
        fclose(f);
        return NULL;
    }
    fseek(f, (long) (sizeof h + h[0]), SEEK_SET);        /* skip the image ID */
    *width = h[12] | (h[13] << 8);
    *height = h[14] | (h[15] << 8);
    bytes = h[16] / 8;
    size = (size_t) bytes * *width * *height;
    buf = malloc(size);
    if (buf && fread(buf, 1, size, f) != size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}
