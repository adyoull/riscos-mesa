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
 * Link: -lbcm_host -lEGL -lOSMesa -lstdc++ -lz -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include "bcm_host.h"
#include <EGL/egl.h>
#include <GLES/gl.h>
#include "hrtime.h"

/* ES 2 entry points (GLES2/gl2.h clashes with GLES/gl.h in one file) */
extern GLuint glCreateShader(GLenum);
extern void glShaderSource(GLuint, GLsizei, const char *const *, const GLint *);
extern void glCompileShader(GLuint);
extern GLuint glCreateProgram(void);
extern void glAttachShader(GLuint, GLuint);
extern void glBindAttribLocation(GLuint, GLuint, const char *);
extern void glLinkProgram(GLuint);
extern void glUseProgram(GLuint);
extern GLint glGetUniformLocation(GLuint, const char *);
extern void glUniformMatrix4fv(GLint, GLsizei, GLboolean, const GLfloat *);
extern void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
extern void glEnableVertexAttribArray(GLuint);
extern void glGetShaderiv(GLuint, GLenum, GLint *);
extern void glGetProgramiv(GLuint, GLenum, GLint *);
#define GL_VERTEX_SHADER   0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82

static const GLfloat cube_v[] = {       /* 6 faces x 2 triangles */
    -1,-1, 1,  1,-1, 1,  1, 1, 1,  -1,-1, 1,  1, 1, 1, -1, 1, 1,
    -1,-1,-1, -1, 1,-1,  1, 1,-1,  -1,-1,-1,  1, 1,-1,  1,-1,-1,
    -1, 1,-1, -1, 1, 1,  1, 1, 1,  -1, 1,-1,  1, 1, 1,  1, 1,-1,
    -1,-1,-1,  1,-1,-1,  1,-1, 1,  -1,-1,-1,  1,-1, 1, -1,-1, 1,
     1,-1,-1,  1, 1,-1,  1, 1, 1,   1,-1,-1,  1, 1, 1,  1,-1, 1,
    -1,-1,-1, -1,-1, 1, -1, 1, 1,  -1,-1,-1, -1, 1, 1, -1, 1,-1,
};
static const GLfloat face_n[6][3] = { {0,0,1}, {0,0,-1}, {0,1,0}, {0,-1,0}, {1,0,0}, {-1,0,0} };
static const GLfloat face_c[6][3] = { {1,.2f,.2f}, {.2f,1,.2f}, {.2f,.2f,1}, {1,1,.2f}, {1,.2f,1}, {.2f,1,1} };
static GLfloat cube_n[36 * 3], cube_c[36 * 4];

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

/* column-major 4x4 helpers for the ES 2 path */
static void mat_mul(GLfloat *r, const GLfloat *a, const GLfloat *b)
{
    GLfloat t[16];
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            t[j * 4 + i] = 0;
            for (k = 0; k < 4; k++) t[j * 4 + i] += a[k * 4 + i] * b[j * 4 + k];
        }
    memcpy(r, t, sizeof t);
}

static void mat_rot(GLfloat *m, float deg, float x, float y, float z)
{
    float a = deg * 3.14159265f / 180, c = cosf(a), s = sinf(a), l = sqrtf(x*x + y*y + z*z);
    x /= l; y /= l; z /= l;
    memset(m, 0, 16 * sizeof *m);
    m[0] = x*x*(1-c)+c;   m[4] = x*y*(1-c)-z*s; m[8]  = x*z*(1-c)+y*s;
    m[1] = y*x*(1-c)+z*s; m[5] = y*y*(1-c)+c;   m[9]  = y*z*(1-c)-x*s;
    m[2] = x*z*(1-c)-y*s; m[6] = y*z*(1-c)+x*s; m[10] = z*z*(1-c)+c;
    m[15] = 1;
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
    int es2 = 0, native = 0, small = 0, i, j, frames = 0;
    double limit = 5, t0, t1, render = 0, t_start;
    float angle = 0;
    GLint u_mvp = -1;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-2")) es2 = 1;
        else if (!strcmp(argv[i], "-n")) native = 1;
        else if (!strcmp(argv[i], "-s")) small = 1;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) limit = atof(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) outf = fopen(argv[++i], "w");
        else { printf("usage: dmxtest [-2] [-n | -s] [-t secs] [-o file]\n"); return 1; }
    }
    for (i = 0; i < 36; i++)
        for (j = 0; j < 3; j++) {
            cube_n[i * 3 + j] = face_n[i / 6][j];
            cube_c[i * 4 + j] = face_c[i / 6][j];
            cube_c[i * 4 + 3] = 1;
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

    glViewport(0, 0, surf_w, surf_h);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.25f, 1);
    if (!es2) {
        static const GLfloat lpos[4] = { 2, 3, 4, 0 };
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustumf(-(float) surf_w / surf_h, (float) surf_w / surf_h, -1, 1, 2, 20);
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL);
        glLightfv(GL_LIGHT0, GL_POSITION, lpos);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, cube_v);
        glNormalPointer(GL_FLOAT, 0, cube_n);
        glColorPointer(4, GL_FLOAT, 0, cube_c);
    } else {
        const char *vs = "#version 100\n"
            "uniform mat4 mvp; attribute vec3 pos; attribute vec4 col; varying vec4 v_col;\n"
            "void main() { gl_Position = mvp * vec4(pos, 1.0); v_col = col; }\n";
        const char *fs = "#version 100\nprecision mediump float; varying vec4 v_col;\n"
            "void main() { gl_FragColor = v_col; }\n";
        GLuint v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
        GLuint prog = glCreateProgram();
        GLint ok = 0;
        glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
        glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
        glAttachShader(prog, v); glAttachShader(prog, f);
        glBindAttribLocation(prog, 0, "pos");
        glBindAttribLocation(prog, 1, "col");
        glLinkProgram(prog);
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) { printf("shader link failed\n"); return 1; }
        glUseProgram(prog);
        u_mvp = glGetUniformLocation(prog, "mvp");
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, cube_v);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, cube_c);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    }

    t_start = hr_seconds();
    while (hr_seconds() - t_start < limit) {
        t0 = hr_seconds();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (!es2) {
            glLoadIdentity();
            glTranslatef(0, 0, -6);
            glRotatef(angle, 1, 0.7f, 0.3f);
        } else {
            GLfloat p[16], rot[16], mvp[16];
            float a = (float) surf_w / surf_h;      /* frustum -a..a, -1..1, 2..20 */
            memset(p, 0, sizeof p);
            p[0] = 2 / a; p[5] = 2; p[10] = -22.0f / 18; p[11] = -1; p[14] = -80.0f / 18;
            mat_rot(rot, angle, 1, 0.7f, 0.3f);
            rot[14] = -6;                           /* then move back */
            mat_mul(mvp, p, rot);
            glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
        }
        glDrawArrays(GL_TRIANGLES, 0, 36);
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
