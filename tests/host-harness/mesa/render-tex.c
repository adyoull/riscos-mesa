/*
 * render-tex.c - riscos-mesa rendering check: texturing as games use it.
 *
 * Prints one line per case, "<case> <hash of the image> ...", compared
 * with expected/render-tex.txt by run.sh.
 *
 * - "compressed": a texture given a generic compressed internal format
 *   (GL_COMPRESSED_RGB/RGBA) renders exactly like the uncompressed one,
 *   because riscos-mesa stores it uncompressed; an explicit S3TC format is
 *   still stored compressed. Also prints GL_TEXTURE_COMPRESSED.
 * - "tex": textured, smooth-shaded quads (flat-on, and tilted in
 *   perspective) for each wrap mode, filter and fog setting. Each is drawn
 *   twice: as a program would (swrast may use its fast textured-triangle
 *   code) and with a white texture on a second unit, which rules the fast
 *   code out and gives the general path's image. "maxdiff" is the largest
 *   difference of any channel between the two (the fast code rounds
 *   slightly differently, by up to 2), "diff" how many pixels differ.
 * - "fastest": the same scenes with GL_PERSPECTIVE_CORRECTION_HINT
 *   GL_FASTEST, where riscos-mesa may trade exactness for speed; maxdiff
 *   shows how far from the exact image.
 * - "mipmap": a mipmapped texture with each mipmap filter, exact
 *   (GL_NICEST, per-pixel level choice) and under GL_FASTEST (one level
 *   per triangle); maxdiff/diff show how far GL_FASTEST is from exact,
 *   and "mean" the average difference of the colour channels in
 *   hundredths. "grid" draws the tilted quad as 8x8 small quads, as games
 *   draw terrain, where one level per triangle comes closest.
 *
 * Usage: render-tex
 * Part of riscos-mesa, MIT licence.
 */
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define W 160
#define H 120
#define TS 32                   /* texture size */

static unsigned char *buf, *ref;

static unsigned hash(const unsigned char *p)
{
    unsigned h = 2166136261u;
    size_t i;
    for (i = 0; i < (size_t)W * H * 4; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

/* A texture with detail at the texel level and a distinct edge */
static void make_image(unsigned char *img)
{
    int x, y;
    for (y = 0; y < TS; y++)
        for (x = 0; x < TS; x++) {
            unsigned char *p = img + (y * TS + x) * 4;
            p[0] = (unsigned char)(x * 255 / (TS - 1));
            p[1] = (unsigned char)(y * 255 / (TS - 1));
            p[2] = ((x ^ y) & 4) ? 230 : 25;
            p[3] = (unsigned char)(128 + ((x + y) & 7) * 16);
        }
    /* a bright border row/column, to show how edges are sampled */
    for (x = 0; x < TS; x++) {
        memset(img + x * 4, 255, 3);
        memset(img + ((TS - 1) * TS + x) * 4, 255, 3);
    }
}

static GLuint tex, white;

static void setup_textures(GLenum internal)
{
    static unsigned char img[TS * TS * 4];
    static const unsigned char w[4] = { 255, 255, 255, 255 };
    make_image(img);
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, TS, TS, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    if (!white) {
        glGenTextures(1, &white);
        glBindTexture(GL_TEXTURE_2D, white);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, w);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, tex);
}

enum { FLAT, TILTED, GRID };   /* GRID: TILTED as 8x8 small quads */

/* One scene: a textured, smooth-shaded quad with texture coordinates
   from -0.6 to 1.6 (so wrapping/clamping shows), over a grey background */
static void scene(int proj, GLenum wrap, GLenum filter, int fog, int general, GLenum hint)
{
    glViewport(0, 0, W, H);
    glClearColor(0.3f, 0.3f, 0.3f, 1);
    glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, hint);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (proj == FLAT)
        glOrtho(-1, 1, -1, 1, -10, 10);
    else
        glFrustum(-0.6, 0.6, -0.45, 0.45, 1, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (proj != FLAT) {
        glTranslatef(0, -0.3f, -3);
        glRotatef(-68, 1, 0, 0);
        glScalef(2.5f, 4, 1);
    }

    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter == GL_NEAREST ? GL_NEAREST : GL_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    /* the white texture on unit 1 changes nothing in the picture but
       keeps swrast off its single-texture fast code */
    glActiveTexture(GL_TEXTURE1);
    if (general) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, white);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    glActiveTexture(GL_TEXTURE0);

    if (fog) {
        const GLfloat c[4] = { 0.6f, 0.65f, 0.7f, 1 };
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, 1.5f);
        glFogf(GL_FOG_END, 8);
        glFogfv(GL_FOG_COLOR, c);
    } else {
        glDisable(GL_FOG);
    }

    glMultiTexCoord2f(GL_TEXTURE1, 0, 0);
    if (proj != GRID) {
        glBegin(GL_QUADS);
        glColor4f(1, 1, 1, 1);         glTexCoord2f(-0.6f, -0.6f); glVertex3f(-0.9f, -0.9f, 0);
        glColor4f(1, 0.8f, 0.6f, 1);   glTexCoord2f(1.6f, -0.6f);  glVertex3f(0.9f, -0.9f, 0);
        glColor4f(0.7f, 1, 0.9f, 1);   glTexCoord2f(1.6f, 1.6f);   glVertex3f(0.9f, 0.9f, 0);
        glColor4f(0.9f, 0.9f, 1, 1);   glTexCoord2f(-0.6f, 1.6f);  glVertex3f(-0.9f, 0.9f, 0);
        glEnd();
    } else {
        /* the same quad as 8x8 small ones (white: colour isn't the point) */
        int i, j;
        glColor4f(1, 1, 1, 1);
        glBegin(GL_QUADS);
        for (j = 0; j < 8; j++)
            for (i = 0; i < 8; i++) {
                float x0 = -0.9f + 1.8f * i / 8, x1 = -0.9f + 1.8f * (i + 1) / 8;
                float y0 = -0.9f + 1.8f * j / 8, y1 = -0.9f + 1.8f * (j + 1) / 8;
                float s0 = -0.6f + 2.2f * i / 8, s1 = -0.6f + 2.2f * (i + 1) / 8;
                float t0 = -0.6f + 2.2f * j / 8, t1 = -0.6f + 2.2f * (j + 1) / 8;
                glTexCoord2f(s0, t0); glVertex3f(x0, y0, 0);
                glTexCoord2f(s1, t0); glVertex3f(x1, y0, 0);
                glTexCoord2f(s1, t1); glVertex3f(x1, y1, 0);
                glTexCoord2f(s0, t1); glVertex3f(x0, y1, 0);
            }
        glEnd();
    }
    glFinish();
}

static void compare(const unsigned char *a, const unsigned char *b, int *maxdiff, int *ndiff)
{
    size_t i;
    *maxdiff = 0;
    *ndiff = 0;
    for (i = 0; i < (size_t)W * H; i++) {
        int c, differs = 0;
        for (c = 0; c < 4; c++) {
            int d = abs(a[i * 4 + c] - b[i * 4 + c]);
            if (d > *maxdiff) *maxdiff = d;
            if (d) differs = 1;
        }
        *ndiff += differs;
    }
}

/* Mean difference of the RGB channels, in hundredths */
static int mean_diff(const unsigned char *a, const unsigned char *b)
{
    size_t i;
    long sum = 0;
    for (i = 0; i < (size_t)W * H * 4; i++)
        if ((i & 3) != 3)
            sum += abs(a[i] - b[i]);
    return (int)(sum * 100 / ((long)W * H * 3));
}

static const char *wrap_name(GLenum w)
{
    return w == GL_REPEAT ? "repeat" : w == GL_CLAMP_TO_EDGE ? "edge" : "clamp";
}

static void textured_cases(GLenum hint, const char *label)
{
    static const GLenum wraps[] = { GL_REPEAT, GL_CLAMP_TO_EDGE, GL_CLAMP };
    static const GLenum filters[] = { GL_NEAREST, GL_LINEAR };
    int p, w, f, fog;

    setup_textures(GL_RGBA);
    for (p = FLAT; p <= TILTED; p++)
        for (w = 0; w < 3; w++)
            for (f = 0; f < 2; f++)
                for (fog = 0; fog < 2; fog++) {
                    int maxdiff, ndiff;
                    unsigned h;
                    scene(p, wraps[w], filters[f], fog, 1, GL_NICEST);
                    memcpy(ref, buf, (size_t)W * H * 4);
                    scene(p, wraps[w], filters[f], fog, 0, hint);
                    h = hash(buf);
                    compare(buf, ref, &maxdiff, &ndiff);
                    printf("%s %s %s %s %s %08x maxdiff %d diff %d\n", label,
                           p == FLAT ? "flat" : "tilted", wrap_name(wraps[w]),
                           filters[f] == GL_NEAREST ? "nearest" : "linear",
                           fog ? "fog" : "nofog", h, maxdiff, ndiff);
                }
}

static const char *const projname[] = { "flat", "tilted", "grid" };

/* A mipmapped copy of the texture: each level box-filtered from the one
   above */
static GLuint mip;

static void setup_mipmaps(void)
{
    static unsigned char img[TS * TS * 4], half[TS * TS * 4];
    int size = TS, level = 0, x, y, c;
    make_image(img);
    glGenTextures(1, &mip);
    glBindTexture(GL_TEXTURE_2D, mip);
    for (;;) {
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
        if (size == 1)
            break;
        for (y = 0; y < size / 2; y++)
            for (x = 0; x < size / 2; x++)
                for (c = 0; c < 4; c++)
                    half[(y * (size / 2) + x) * 4 + c] = (unsigned char)
                        ((img[((2 * y) * size + 2 * x) * 4 + c] + img[((2 * y) * size + 2 * x + 1) * 4 + c] +
                          img[((2 * y + 1) * size + 2 * x) * 4 + c] + img[((2 * y + 1) * size + 2 * x + 1) * 4 + c] + 2) / 4);
        size /= 2;
        level++;
        memcpy(img, half, (size_t)size * size * 4);
    }
}

static void mipmap_cases(void)
{
    static const GLenum mins[] = { GL_NEAREST_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_NEAREST,
                                   GL_NEAREST_MIPMAP_LINEAR, GL_LINEAR_MIPMAP_LINEAR };
    static const char *const names[] = { "nearest-nearest", "linear-nearest",
                                         "nearest-linear", "linear-linear" };
    int p, m;
    GLuint saved = tex;

    setup_mipmaps();
    tex = mip;
    for (p = FLAT; p <= GRID; p++)
        for (m = 0; m < 4; m++) {
            int maxdiff, ndiff;
            unsigned h;
            /* scene() sets the filters; give it the mipmap one for MIN */
            scene(p, GL_REPEAT, mins[m], 0, 0, GL_NICEST);
            memcpy(ref, buf, (size_t)W * H * 4);
            h = hash(buf);
            printf("mipmap %s %s exact %08x\n", projname[p], names[m], h);
            scene(p, GL_REPEAT, mins[m], 0, 0, GL_FASTEST);
            compare(buf, ref, &maxdiff, &ndiff);
            printf("mipmap %s %s fastest %08x maxdiff %d diff %d mean %d\n", projname[p],
                   names[m], hash(buf), maxdiff, ndiff, mean_diff(buf, ref));
        }
    tex = saved;
}

static void compressed_cases(void)
{
    static const struct { GLenum internal; const char *name; } fmts[] = {
        { GL_RGBA, "rgba" },
        { GL_COMPRESSED_RGBA, "compressed-rgba" },
        { GL_COMPRESSED_RGB, "compressed-rgb" },
        { GL_RGB, "rgb" },
        { GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, "s3tc-dxt3" },
    };
    int i;
    for (i = 0; i < 5; i++) {
        GLint compressed = -1;
        setup_textures(fmts[i].internal);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &compressed);
        scene(TILTED, GL_REPEAT, GL_LINEAR, 0, 0, GL_NICEST);
        printf("compressed %s %08x compressed %d\n", fmts[i].name, hash(buf), compressed);
    }
}

int main(void)
{
    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
    buf = malloc((size_t)W * H * 4);
    ref = malloc((size_t)W * H * 4);
    if (!ctx || !buf || !ref || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
        fprintf(stderr, "OSMesa setup failed\n");
        return 1;
    }
    OSMesaPixelStore(OSMESA_Y_UP, 0);

    compressed_cases();
    textured_cases(GL_NICEST, "tex");
    textured_cases(GL_FASTEST, "fastest");
    mipmap_cases();
    OSMesaDestroyContext(ctx);
    return 0;
}
