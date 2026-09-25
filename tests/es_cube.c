/*
 * es_cube.c - a spinning cube in OpenGL ES 1.1 (lit, fixed function) or
 * OpenGL ES 2.0 (GLSL ES shaders), shared by dmxtest (DispmanX) and
 * glestest (native RISC OS EGL). MIT licence.
 */
#include <string.h>
#include <math.h>
#include <GLES/gl.h>
#include "es_cube.h"

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

static int use_es2;
static GLint u_mvp = -1;
static float aspect = 1;

int es_cube_init(int es2, int w, int h)
{
    int i, j;
    use_es2 = es2;
    aspect = (float) w / h;
    for (i = 0; i < 36; i++)
        for (j = 0; j < 3; j++) {
            cube_n[i * 3 + j] = face_n[i / 6][j];
            cube_c[i * 4 + j] = face_c[i / 6][j];
            cube_c[i * 4 + 3] = 1;
        }
    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.25f, 1);
    if (!es2) {
        static const GLfloat lpos[4] = { 2, 3, 4, 0 };
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustumf(-aspect, aspect, -1, 1, 2, 20);
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
        if (!ok)
            return 0;
        glUseProgram(prog);
        u_mvp = glGetUniformLocation(prog, "mvp");
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, cube_v);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, cube_c);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    }
    return 1;
}

void es_cube_resize(int w, int h)
{
    aspect = (float) w / h;
    glViewport(0, 0, w, h);
    if (!use_es2) {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustumf(-aspect, aspect, -1, 1, 2, 20);
        glMatrixMode(GL_MODELVIEW);
    }
}

void es_cube_draw(float angle)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!use_es2) {
        glLoadIdentity();
        glTranslatef(0, 0, -6);
        glRotatef(angle, 1, 0.7f, 0.3f);
    } else {
        GLfloat p[16], rot[16], mvp[16];       /* frustum -a..a, -1..1, 2..20 */
        memset(p, 0, sizeof p);
        p[0] = 2 / aspect; p[5] = 2; p[10] = -22.0f / 18; p[11] = -1; p[14] = -80.0f / 18;
        mat_rot(rot, angle, 1, 0.7f, 0.3f);
        rot[14] = -6;                           /* then move back */
        mat_mul(mvp, p, rot);
        glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
    }
    glDrawArrays(GL_TRIANGLES, 0, 36);
}
