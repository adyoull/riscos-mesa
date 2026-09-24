/*
 * osmesatest - standalone OSMesa check for RISC OS (no SDL needed).
 *
 * Renders into a 32bpp RISC OS sprite in memory (0x00BBGGRR == OSMESA_RGBA),
 * times a fixed-function scene and a GLSL 1.20 scene, prints the GL strings
 * and frame rates, and saves the last frame as a Sprite file "gltest" (&FF9)
 * in the current directory so it can be viewed with Paint.
 *
 * Usage: osmesatest [width height frames]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/osmesa.h>

#define SPRITE_MODE_32BPP (1 | (90 << 1) | (90 << 14) | (6 << 27))

/* In-memory sprite area with one sprite, built by hand so this program
   needs no SWIs at all (portable to a Linux host for comparison runs). */
typedef struct {
    int size, count, start, end;           /* area header */
    int next; char name[12]; int width, height, first_bit, last_bit,
        image_offset, mask_offset, mode;    /* sprite header */
} sprite_hdr;

static unsigned char *make_sprite(int w, int h, sprite_hdr **hdr_out)
{
    size_t img = (size_t)w * h * 4;
    sprite_hdr *s = calloc(1, sizeof(sprite_hdr) + img);
    if (!s) return NULL;
    s->size = sizeof(sprite_hdr) + img;
    s->count = 1;
    s->start = 16;
    s->end = s->size;
    s->next = sizeof(sprite_hdr) - 16 + img;
    strncpy(s->name, "gltest", 12);
    s->width = w - 1;                       /* words - 1 (32bpp: 1 px/word) */
    s->height = h - 1;
    s->first_bit = 0;
    s->last_bit = 31;
    s->image_offset = sizeof(sprite_hdr) - 16;
    s->mask_offset = s->image_offset;       /* no mask */
    s->mode = SPRITE_MODE_32BPP;
    *hdr_out = s;
    return (unsigned char *)(s + 1);
}

static int save_sprite(sprite_hdr *s)
{
    /* Sprite file = area without its first word. UnixLib maps ",ff9". */
    FILE *f = fopen("gltest,ff9", "wb");
    if (!f) return -1;
    fwrite(&s->count, 1, s->size - 4, f);
    fclose(f);
    return 0;
}

static double now(void)
{
    return (double)clock() / CLOCKS_PER_SEC;
}

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
            glVertex3f(f[i][j][0], f[i][j][1], f[i][j][2]);
    }
    glEnd();
}

static void setup_view(int w, int h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0 * w / h, 1.0 * w / h, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -6);
}

static double run_fixed(int w, int h, int frames)
{
    static const float lpos[4] = {2, 3, 4, 0};
    double t0;
    int i;

    setup_view(w, h);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glLightfv(GL_LIGHT0, GL_POSITION, lpos);
    glShadeModel(GL_SMOOTH);

    t0 = now();
    for (i = 0; i < frames; i++) {
        glClearColor(0.1f, 0.1f, 0.2f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPushMatrix();
        glRotatef(i * 3.0f, 1, 0, 0);
        glRotatef(i * 2.0f, 0, 1, 0);
        cube();
        glPopMatrix();
        glFinish();
    }
    glDisable(GL_LIGHTING);
    return frames / (now() - t0 + 1e-9);
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    char log[512];
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        printf("  shader compile failed: %s\n", log);
        return 0;
    }
    return s;
}

static double run_glsl(int w, int h, int frames)
{
    static const char *vs =
        "#version 120\n"
        "varying vec3 n; varying vec3 col;\n"
        "void main() { n = gl_NormalMatrix * gl_Normal; col = gl_Color.rgb;\n"
        "  gl_Position = ftransform(); }\n";
    static const char *fs =
        "#version 120\n"
        "varying vec3 n; varying vec3 col; uniform float t;\n"
        "void main() { float d = max(dot(normalize(n), normalize(vec3(0.4,0.6,0.8))), 0.0);\n"
        "  float stripe = 0.5 + 0.5 * sin(gl_FragCoord.y * 0.1 + t);\n"
        "  gl_FragColor = vec4(col * (0.25 + 0.75 * d) * (0.7 + 0.3 * stripe), 1.0); }\n";
    GLuint v, f, p;
    GLint ok = 0, tl;
    double t0;
    int i;

    v = compile(GL_VERTEX_SHADER, vs);
    f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return -1;
    p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { printf("  link failed\n"); return -1; }
    glUseProgram(p);
    tl = glGetUniformLocation(p, "t");

    setup_view(w, h);
    glEnable(GL_DEPTH_TEST);
    t0 = now();
    for (i = 0; i < frames; i++) {
        glUniform1f(tl, i * 0.2f);
        glClearColor(0.15f, 0.1f, 0.1f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glPushMatrix();
        glRotatef(i * 3.0f, 1, 0, 0);
        glRotatef(i * 2.0f, 0, 1, 0);
        cube();
        glPopMatrix();
        glFinish();
    }
    glUseProgram(0);
    return frames / (now() - t0 + 1e-9);
}

int main(int argc, char **argv)
{
    int w = 320, h = 240, frames = 100;
    OSMesaContext ctx;
    sprite_hdr *spr;
    unsigned char *buf;
    double fps;

    if (argc >= 4) { w = atoi(argv[1]); h = atoi(argv[2]); frames = atoi(argv[3]); }

    ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
    if (!ctx) { printf("OSMesaCreateContextExt failed\n"); return 1; }
    buf = make_sprite(w, h, &spr);
    if (!buf) { printf("out of memory\n"); return 1; }
    if (!OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, w, h)) {
        printf("OSMesaMakeCurrent failed\n"); return 1;
    }
    OSMesaPixelStore(OSMESA_Y_UP, 0);       /* sprite rows are top-down */

    printf("GL_VENDOR   : %s\n", glGetString(GL_VENDOR));
    printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    printf("GLSL        : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("Size %dx%d, %d frames per test\n", w, h, frames);

    fps = run_fixed(w, h, frames);
    printf("Fixed-function lit cube : %.1f fps\n", fps);
    fps = run_glsl(w, h, frames);
    if (fps >= 0) printf("GLSL 1.20 shaded cube   : %.1f fps\n", fps);

    if (save_sprite(spr) == 0) printf("Last frame saved as sprite file 'gltest'\n");
    else printf("Could not save sprite file\n");

    OSMesaDestroyContext(ctx);
    free(spr);
    return 0;
}
