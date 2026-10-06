/*
 * glsl-es2compat.c - riscos-mesa rendering check: GL_ARB_ES2_compatibility
 * in an OpenGL 2.1 (compatibility) context (patch riscos-es-extras).
 *
 *   - the extension is listed, and the GL version is still 2.1;
 *   - an OpenGL ES 2.0 shader pair (#version 100, precision qualifiers)
 *     compiles, links and draws exactly what the same shaders written as
 *     GLSL 1.20 draw;
 *   - GL_FIXED vertex attributes draw what the same float ones do;
 *   - glClearDepthf, glDepthRangef, glGetShaderPrecisionFormat,
 *     glReleaseShaderCompiler, GL_MAX_VARYING_VECTORS and friends, and
 *     GL_IMPLEMENTATION_COLOR_READ_FORMAT/TYPE answer without errors;
 *   - glShaderBinary is refused (no binary formats), as the extension
 *     allows.
 * One line per case; run.sh compares them with expected/glsl-es2compat.txt.
 * Part of riscos-mesa, MIT licence.
 */
#define GL_GLEXT_PROTOTYPES
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 96
#define H 72

static unsigned char buf[W * H * 4];

static unsigned hash(void)
{
    unsigned h = 2166136261u;
    int i;
    for (i = 0; i < W * H * 4; i++) {
        h ^= buf[i];
        h *= 16777619u;
    }
    return h;
}

static GLuint shader(GLenum type, const char *src, int *ok)
{
    GLuint s = glCreateShader(type);
    GLint st;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &st);
    if (!st) {
        char log[1000];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        printf("compile failed: %s\n", log);
        *ok = 0;
    }
    return s;
}

static GLuint program(const char *vs, const char *fs, int *ok)
{
    GLuint p = glCreateProgram();
    GLint st;
    glAttachShader(p, shader(GL_VERTEX_SHADER, vs, ok));
    glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs, ok));
    glBindAttribLocation(p, 0, "pos");
    glBindAttribLocation(p, 1, "col");
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &st);
    if (!st) *ok = 0;
    return p;
}

static const char *VS100 =
    "#version 100\n"
    "attribute vec2 pos; attribute vec3 col; varying lowp vec3 c; uniform mediump float k;\n"
    "void main() { c = col * k; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *FS100 =
    "#version 100\nprecision mediump float;\n"
    "varying lowp vec3 c;\n"
    "void main() { gl_FragColor = vec4(fract(c * 3.0), 1.0); }\n";
static const char *VS120 =
    "#version 120\n"
    "attribute vec2 pos; attribute vec3 col; varying vec3 c; uniform float k;\n"
    "void main() { c = col * k; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *FS120 =
    "#version 120\n"
    "varying vec3 c;\n"
    "void main() { gl_FragColor = vec4(fract(c * 3.0), 1.0); }\n";

static const GLfloat tri[] = { -0.9f, -0.8f, 1, 0, 0.25f,  0.8f, -0.6f, 0, 1, 0.5f,
                               -0.2f, 0.9f, 0.3f, 0.6f, 1 };

/* draw the triangle with program p; fixed: positions as GL_FIXED */
static unsigned draw(GLuint p, int fixed)
{
    GLint fx[6];
    int i;
    glUseProgram(p);
    glUniform1f(glGetUniformLocation(p, "k"), 0.9f);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (fixed) {
        for (i = 0; i < 3; i++) {
            fx[i * 2] = (GLint) (tri[i * 5] * 65536.0f);
            fx[i * 2 + 1] = (GLint) (tri[i * 5 + 1] * 65536.0f);
        }
        glVertexAttribPointer(0, 2, GL_FIXED, GL_FALSE, 0, fx);
    } else
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, tri);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, tri + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    return hash();
}

static int has_ext(const char *name)
{
    const char *e = (const char *) glGetString(GL_EXTENSIONS);
    size_t n = strlen(name);
    while (e && (e = strstr(e, name)) != NULL) {
        if (e[n] == ' ' || e[n] == 0) return 1;
        e += n;
    }
    return 0;
}

int main(void)
{
    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
    GLuint p100, p120;
    int ok = 1;
    unsigned h100, h120, hfix;

    OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H);
    printf("GL_ARB_ES2_compatibility %s, version %.3s\n",
           has_ext("GL_ARB_ES2_compatibility") ? "listed" : "MISSING",
           (const char *) glGetString(GL_VERSION));

    p100 = program(VS100, FS100, &ok);
    printf("#version 100 shaders: %s\n", ok ? "compiled and linked" : "FAIL");
    ok = 1;
    p120 = program(VS120, FS120, &ok);
    h100 = draw(p100, 0);
    h120 = draw(p120, 0);
    {
        int i, lit = 0;
        for (i = 0; i < W * H; i++)
            lit += buf[i * 4] || buf[i * 4 + 1] || buf[i * 4 + 2];
        printf("#version 100 draws what #version 120 does: %s (%08x)\n",
               h100 == h120 && lit > W * H / 8 ? "ok" : "FAIL", h100);
    }
    hfix = draw(p120, 1);
    printf("GL_FIXED positions draw what floats do: %s (error %#x)\n",
           hfix == h120 ? "ok" : "FAIL", glGetError());

    {
        GLfloat v[2];
        glClearDepthf(0.25f);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, v);
        printf("glClearDepthf: %s\n", v[0] == 0.25f ? "ok" : "FAIL");
        glDepthRangef(0.125f, 0.75f);
        glGetFloatv(GL_DEPTH_RANGE, v);
        printf("glDepthRangef: %s\n", v[0] == 0.125f && v[1] == 0.75f ? "ok" : "FAIL");
        glDepthRangef(0, 1);
    }
    {
        GLint range[2] = { -1, -1 }, prec = -1;
        glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, range, &prec);
        printf("glGetShaderPrecisionFormat high float: range %d,%d precision %d, error %#x\n",
               range[0], range[1], prec, glGetError());
        glReleaseShaderCompiler();
        printf("glReleaseShaderCompiler: error %#x\n", glGetError());
    }
    {
        GLint n = 0, vv = 0, fv = 0, fmt = 0, type = 0;
        glGetIntegerv(GL_MAX_VARYING_VECTORS, &n);
        glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vv);
        glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &fv);
        printf("varying/uniform vectors %d/%d/%d, error %#x\n", n, vv, fv, glGetError());
        glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &fmt);
        glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &type);
        printf("implementation read format %#x type %#x, error %#x\n", fmt, type, glGetError());
    }
    {
        GLint nfmt = -1;
        GLuint s = glCreateShader(GL_VERTEX_SHADER);
        glGetIntegerv(GL_NUM_SHADER_BINARY_FORMATS, &nfmt);
        glShaderBinary(1, &s, 0, "x", 1);
        printf("shader binary formats %d; glShaderBinary refused: %s\n", nfmt,
               glGetError() != GL_NO_ERROR ? "ok" : "FAIL");
    }
    OSMesaDestroyContext(ctx);
    return 0;
}
