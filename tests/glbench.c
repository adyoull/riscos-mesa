/*
 * glbench - software OpenGL (OSMesa) benchmark, no SDL, no desktop.
 * Renders six scenes into an off-screen 32bpp buffer and reports the time
 * per frame, so builds made with different compiler flags can be compared
 * on the same machine. Run from a TaskWindow; redirect to keep the result:
 *     glbench > result
 * Usage: glbench [width height seconds_per_scene]   (default 640 480 2)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/osmesa.h>
#include "hrtime.h"

#ifndef VARIANT
#define VARIANT "unnamed build"
#endif

static int W = 640, H = 480;
static double secs = 2.0;

static void view(void)
{
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-1.0 * W / H, 1.0 * W / H, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}

static void ortho(void)
{
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1, 0, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
}

static void cube(void)
{
    static const float n[6][3] = {{0,0,1},{0,0,-1},{0,1,0},{0,-1,0},{1,0,0},{-1,0,0}};
    static const float c[6][3] = {{1,.3f,.3f},{.3f,1,.3f},{.3f,.3f,1},{1,1,.3f},{1,.3f,1},{.3f,1,1}};
    static const int f[6][4][3] = {
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}}, {{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1},{ 1,-1,-1}},
        {{-1, 1,-1},{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1}}, {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},
        {{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1},{ 1,-1, 1}}, {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}}};
    int i, j;
    glBegin(GL_QUADS);
    for (i = 0; i < 6; i++) {
        glColor3fv(c[i]); glNormal3fv(n[i]);
        for (j = 0; j < 4; j++) glVertex3f((float)f[i][j][0], (float)f[i][j][1], (float)f[i][j][2]);
    }
    glEnd();
}

/* ---------------- scenes ---------------- */
static void s_clear(int i)
{
    glClearColor((i & 1) * 0.2f, 0.1f, 0.2f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void lit_setup(void)
{
    static const float lpos[4] = {2, 3, 4, 0};
    view(); glTranslatef(0, 0, -4.5f);
    glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL); glLightfv(GL_LIGHT0, GL_POSITION, lpos);
}

static void s_cube(int i)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPushMatrix(); glRotatef(i * 3.0f, 1, 0, 0); glRotatef(i * 2.0f, 0, 1, 0);
    cube(); glPopMatrix();
}

static GLuint tex;
static void tex_setup(void)
{
    static unsigned char img[256 * 256 * 4];
    int x, y;
    for (y = 0; y < 256; y++) for (x = 0; x < 256; x++) {
        unsigned char *p = img + (y * 256 + x) * 4;
        int c = ((x >> 5) ^ (y >> 5)) & 1;
        p[0] = c ? 230 : (unsigned char)x; p[1] = (unsigned char)y; p[2] = c ? 60 : 200; p[3] = 255;
    }
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    ortho(); glEnable(GL_TEXTURE_2D);
}

static void s_tex(int i)
{
    float o = (i % 64) / 64.0f;
    glBegin(GL_QUADS);
    glTexCoord2f(o, 0); glVertex2f(0, 0); glTexCoord2f(o + 2, 0); glVertex2f(1, 0);
    glTexCoord2f(o + 2, 2); glVertex2f(1, 1); glTexCoord2f(o, 2); glVertex2f(0, 1);
    glEnd();
}

static void blend_setup(void)
{
    ortho(); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void s_blend(int i)
{
    int k;
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    for (k = 0; k < 4; k++) {
        glColor4f(k == 0, k == 1, k >= 2, 0.3f + 0.1f * ((i + k) & 3));
        glRectf(0, 0, 1, 1);
    }
}

#define SLICES 48
#define STACKS 32
static float sph_v[(STACKS + 1) * (SLICES + 1) * 3];
static GLushort sph_i[STACKS * SLICES * 6];
static void tris_setup(void)
{
    int a, b, n = 0;
    for (a = 0; a <= STACKS; a++) for (b = 0; b <= SLICES; b++) {
        double th = M_PI * a / STACKS, ph = 2 * M_PI * b / SLICES;
        float *v = sph_v + (a * (SLICES + 1) + b) * 3;
        v[0] = (float)(sin(th) * cos(ph)); v[1] = (float)cos(th); v[2] = (float)(sin(th) * sin(ph));
    }
    for (a = 0; a < STACKS; a++) for (b = 0; b < SLICES; b++) {
        GLushort p = (GLushort)(a * (SLICES + 1) + b), q = (GLushort)(p + SLICES + 1);
        sph_i[n++] = p; sph_i[n++] = q; sph_i[n++] = (GLushort)(p + 1);
        sph_i[n++] = (GLushort)(p + 1); sph_i[n++] = q; sph_i[n++] = (GLushort)(q + 1);
    }
    lit_setup();
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, sph_v); glNormalPointer(GL_FLOAT, 0, sph_v);
    glColor3f(0.9f, 0.7f, 0.3f);
}

static void s_tris(int i)
{
    int k;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    for (k = 0; k < 4; k++) {      /* 4 small spheres = 12288 triangles */
        glPushMatrix();
        glTranslatef((k & 1) ? 1.2f : -1.2f, (k & 2) ? 0.9f : -0.9f, 0);
        glRotatef(i * 2.0f + k * 30, 0, 1, 0); glScalef(0.7f, 0.7f, 0.7f);
        glDrawElements(GL_TRIANGLES, STACKS * SLICES * 6, GL_UNSIGNED_SHORT, sph_i);
        glPopMatrix();
    }
}

static GLint t_loc;
static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type); GLint ok = 0;
    glShaderSource(s, 1, &src, NULL); glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    return ok ? s : 0;
}
static int glsl_setup(void)
{
    static const char *vs =
        "#version 120\nvarying vec3 n; varying vec3 col;\n"
        "void main() { n = gl_NormalMatrix * gl_Normal; col = gl_Color.rgb; gl_Position = ftransform(); }\n";
    static const char *fs =
        "#version 120\nvarying vec3 n; varying vec3 col; uniform float t;\n"
        "void main() { float d = max(dot(normalize(n), normalize(vec3(0.4,0.6,0.8))), 0.0);\n"
        "  float s = 0.5 + 0.5 * sin(gl_FragCoord.y * 0.1 + t);\n"
        "  gl_FragColor = vec4(col * (0.25 + 0.75 * d) * (0.7 + 0.3 * s), 1.0); }\n";
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs), p;
    GLint ok = 0;
    if (!v || !f) return 0;
    p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) return 0;
    glUseProgram(p); t_loc = glGetUniformLocation(p, "t");
    view(); glTranslatef(0, 0, -4.5f); glEnable(GL_DEPTH_TEST);
    return 1;
}
static void s_glsl(int i)
{
    glUniform1f(t_loc, i * 0.2f);
    s_cube(i);
}

/* ---------------- harness ---------------- */
static void reset_state(void)
{
    glUseProgram(0);
    glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND);
    glDisableClientState(GL_VERTEX_ARRAY); glDisableClientState(GL_NORMAL_ARRAY);
    glColor4f(1, 1, 1, 1);
}

static void run(const char *name, const char *what, void (*frame)(int))
{
    double t0 = hr_seconds(), t, best = 1e9;
    int n = 0;
    frame(0); glFinish();                           /* warm-up */
    t0 = hr_seconds();
    do {
        double f0 = hr_seconds();
        frame(n++); glFinish();
        t = hr_seconds();
        if (t - f0 < best) best = t - f0;
    } while (t - t0 < secs || n < 5);
    printf("%-6s %8.2f ms %8.1f fps   (best %6.2f ms)  %s\n",
           name, (t - t0) * 1000 / n, n / (t - t0), best * 1000, what);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    OSMesaContext ctx;
    unsigned char *buf;
    double start;
    if (argc >= 3) { W = atoi(argv[1]); H = atoi(argv[2]); }
    if (argc >= 4) secs = atof(argv[3]);

    ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
    buf = malloc((size_t)W * H * 4);
    if (!ctx || !buf || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
        printf("OSMesa setup failed\n"); return 1;
    }
    OSMesaPixelStore(OSMESA_Y_UP, 0);

    printf("glbench: %s\n%s / %s\n%dx%d, %.1f s per scene, timer: %s\n\n",
           VARIANT, glGetString(GL_RENDERER), glGetString(GL_VERSION), W, H, secs, hr_source());
    start = hr_seconds();
    reset_state();                       run("clear",  "clear colour + depth", s_clear);
    reset_state(); lit_setup();          run("cube",   "lit, smooth, depth-tested cube (fixed function)", s_cube);
    reset_state(); tex_setup();          run("tex",    "full-screen bilinear textured quad", s_tex);
    reset_state(); blend_setup();        run("blend",  "4 full-screen alpha-blended quads", s_blend);
    reset_state(); tris_setup();         run("tris",   "12288 lit triangles via vertex arrays", s_tris);
    reset_state();
    if (glsl_setup())                    run("glsl",   "GLSL 1.20 per-pixel shaded cube", s_glsl);
    else printf("glsl   shader compile failed\n");
    printf("\ntotal %.1f s\n", hr_seconds() - start);
    OSMesaDestroyContext(ctx);
    return 0;
}
