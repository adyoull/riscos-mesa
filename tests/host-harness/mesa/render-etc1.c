/*
 * render-etc1.c - riscos-mesa rendering check: ETC1 textures
 * (GL_OES_compressed_ETC1_RGB8_texture) in OpenGL ES 1.1 and 2.0.
 *
 * riscos-mesa decodes ETC1 when a texture is loaded and keeps it as an
 * ordinary RGB texture (patch riscos-es-extras). This checks:
 *   - every texel against this file's own ETC1 decoder (written from the
 *     Khronos specification, so it doesn't share Mesa's code), for a
 *     texture whose size isn't a multiple of the 4x4 block, in ES 2.0
 *     (shader) and ES 1.1 (GL_REPLACE), and its mipmap levels (loaded in
 *     both, sampled in ES 1.1);
 *   - glCompressedTexSubImage2D (GL_EXT_compressed_ETC1_RGB8_sub_texture):
 *     a block-aligned region, and a region ending at the image's edge;
 *   - the errors: an unaligned offset, a wrong image size, and
 *     glTexSubImage2D / glCopyTexSubImage2D on an ETC1 texture;
 *   - a perspective, bilinear-filtered draw of the ETC1 texture is the same,
 *     bit for bit, as the same texels loaded with glTexImage2D(GL_RGB), so
 *     it takes the same (fast) path.
 * One line per case; run.sh compares them with expected/render-etc1.txt.
 * Part of riscos-mesa, MIT licence.
 */
#define GL_GLEXT_PROTOTYPES
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OpenGL ES 1.1's float version (not in GL/gl.h) */
GLAPI void APIENTRY glFrustumf(GLfloat l, GLfloat r, GLfloat b, GLfloat t, GLfloat n, GLfloat f);

#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

#define TW 18          /* not a multiple of 4: partial blocks */
#define TH 14
#define BW ((TW + 3) / 4)
#define BH ((TH + 3) / 4)
#define VW 160
#define VH 120

static unsigned char win[VW * VH * 4];
static unsigned rng = 12345;

static unsigned rnd(void)
{
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

/* A random block that is valid ETC1: in differential mode, the second
 * colour (base + 3-bit signed delta) must stay within 0..31. */
static void random_block(unsigned char *b)
{
    int i, c;
    for (i = 0; i < 8; i++)
        b[i] = rnd() & 0xFF;
    if (b[3] & 2) {
        for (c = 0; c < 3; c++) {
            int base = b[c] >> 3, d = b[c] & 7;
            if (d >= 4) d -= 8;
            if (base + d < 0 || base + d > 31)
                b[c] = (unsigned char) ((base << 3) | 0);   /* delta 0 */
        }
    }
}

static const int modtab[8][4] = {
    { 2, 8, -2, -8 }, { 5, 17, -5, -17 }, { 9, 29, -9, -29 }, { 13, 42, -13, -42 },
    { 18, 60, -18, -60 }, { 24, 80, -24, -80 }, { 33, 106, -33, -106 }, { 47, 183, -47, -183 }
};

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* Decode one block's texel (x, y) (0..3) to RGB, from the specification. */
static void decode_texel(const unsigned char *b, int x, int y, unsigned char *rgb)
{
    int diff = (b[3] >> 1) & 1, flip = b[3] & 1;
    int sub = flip ? (y >= 2) : (x >= 2);
    int base[3], c, cw, i, msb, lsb, m;
    for (c = 0; c < 3; c++) {
        if (diff) {
            int b1 = b[c] >> 3, d = b[c] & 7;
            if (d >= 4) d -= 8;
            int v = sub ? b1 + d : b1;
            base[c] = (v << 3) | (v >> 2);
        } else {
            int v = sub ? (b[c] & 0xF) : (b[c] >> 4);
            base[c] = (v << 4) | v;
        }
    }
    cw = sub ? (b[3] >> 2) & 7 : (b[3] >> 5) & 7;
    i = x * 4 + y;
    msb = ((((b[4] << 8) | b[5]) >> i) & 1);
    lsb = ((((b[6] << 8) | b[7]) >> i) & 1);
    m = modtab[cw][(msb << 1) | lsb];
    for (c = 0; c < 3; c++)
        rgb[c] = (unsigned char) clamp255(base[c] + m);
}

/* the image's texel (x, y) from blocks laid out bw blocks to a row */
static void image_texel(const unsigned char *blocks, int bw, int x, int y, unsigned char *rgb)
{
    decode_texel(blocks + ((y / 4) * bw + x / 4) * 8, x & 3, y & 3, rgb);
}

static const char *VS =
    "attribute vec2 p; attribute vec2 t; varying vec2 v;\n"
    "void main() { v = t; gl_Position = vec4(p, 0.0, 1.0); }\n";
static const char *FS =
    "precision mediump float; varying vec2 v; uniform sampler2D s;\n"
    "void main() { gl_FragColor = texture2D(s, v); }\n";

static GLuint prog;

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

/* Draw the bound texture over a w x h viewport, texel for pixel, and read
 * it back; es2: through the shader, else ES 1.1 fixed function. */
static void draw_texels(int es2, int w, int h, unsigned char *out)
{
    static const GLfloat quad[] = { -1, -1, 0, 0, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1 };
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (es2) {
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, quad);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, quad + 2);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    } else {
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(2, GL_FLOAT, 16, quad);
        glTexCoordPointer(2, GL_FLOAT, 16, quad + 2);
    }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
}

/* Count the texels of a w x h level that differ from the decoder's. */
static int compare(const unsigned char *blocks, int w, int h, const unsigned char *px)
{
    int x, y, bad = 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned char rgb[3];
            const unsigned char *p = px + (y * w + x) * 4;
            image_texel(blocks, (w + 3) / 4, x, y, rgb);
            if (p[0] != rgb[0] || p[1] != rgb[1] || p[2] != rgb[2] || p[3] != 255)
                bad++;
        }
    return bad;
}

static OSMesaContext make(int profile)
{
    const int attr[] = { OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 0,
                         OSMESA_PROFILE, profile, 0 };
    OSMesaContext c = OSMesaCreateContextAttribs(attr, NULL);
    if (!c || !OSMesaMakeCurrent(c, win, GL_UNSIGNED_BYTE, VW, VH)) {
        printf("FAIL: no context (profile %#x)\n", profile);
        exit(1);
    }
    return c;
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

static unsigned char tex[BW * BH * 8];

static void decode_cases(int es2)
{
    static unsigned char px[VW * VH * 4];
    const char *api = es2 ? "ES 2.0" : "ES 1.1";
    OSMesaContext c = make(es2 ? 0x1002 : 0x1001);
    GLuint t;
    int bad, level, w, h;
    (void) w; (void) h;

    printf("%s: GL_OES_compressed_ETC1_RGB8_texture %s, sub_texture %s\n", api,
           has_ext("GL_OES_compressed_ETC1_RGB8_texture") ? "listed" : "MISSING",
           has_ext("GL_EXT_compressed_ETC1_RGB8_sub_texture") ? "listed" : "MISSING");
    if (es2) {
        prog = glCreateProgram();
        glAttachShader(prog, shader(GL_VERTEX_SHADER, VS));
        glAttachShader(prog, shader(GL_FRAGMENT_SHADER, FS));
        glBindAttribLocation(prog, 0, "p");
        glBindAttribLocation(prog, 1, "t");
        glLinkProgram(prog);
        glUseProgram(prog);
    }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, TW, TH, 0, sizeof tex, tex);
    printf("%s: load %dx%d: error %#x\n", api, TW, TH, glGetError());
    draw_texels(es2, TW, TH, px);
    bad = compare(tex, TW, TH, px);
    printf("%s: decoded texels: %s (%d differ)\n", api, bad ? "FAIL" : "ok", bad);

    /* replace a block-aligned 8x4 region at (4, 4) */
    {
        unsigned char sub[2 * 1 * 8], full[BW * BH * 8];
        int i;
        for (i = 0; i < 2; i++) random_block(sub + i * 8);
        glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 4, 4, 8, 4, GL_ETC1_RGB8_OES, sizeof sub, sub);
        printf("%s: sub image 8x4 at 4,4: error %#x\n", api, glGetError());
        memcpy(full, tex, sizeof full);
        memcpy(full + (1 * BW + 1) * 8, sub, 16);
        draw_texels(es2, TW, TH, px);
        bad = compare(full, TW, TH, px);
        printf("%s: after the sub image: %s (%d differ)\n", api, bad ? "FAIL" : "ok", bad);
        /* a region ending at the right and top edges: 2x2 at (16, 12) */
        random_block(sub);
        glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 16, 12, 2, 2, GL_ETC1_RGB8_OES, 8, sub);
        printf("%s: edge sub image 2x2 at 16,12: error %#x\n", api, glGetError());
        memcpy(full + (3 * BW + 4) * 8, sub, 8);
        draw_texels(es2, TW, TH, px);
        bad = compare(full, TW, TH, px);
        printf("%s: after the edge sub image: %s (%d differ)\n", api, bad ? "FAIL" : "ok", bad);
        memcpy(tex, full, sizeof tex);
    }

    /* errors */
    {
        unsigned char blk[8] = { 0 };
        glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 2, 0, 4, 4, GL_ETC1_RGB8_OES, 8, blk);
        printf("%s: unaligned sub image offset: error %#x\n", api, glGetError());
        glCompressedTexImage2D(GL_TEXTURE_2D, 1, GL_ETC1_RGB8_OES, 8, 8, 0, 8, blk);
        printf("%s: wrong image size: error %#x\n", api, glGetError());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, blk);
        printf("%s: glTexSubImage2D on ETC1: error %#x\n", api, glGetError());
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 4, 4);
        printf("%s: glCopyTexSubImage2D on ETC1: error %#x\n", api, glGetError());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 4, 4, 0, GL_RGB, GL_UNSIGNED_BYTE, blk);
        printf("%s: glTexImage2D with ETC1: error %#x\n", api, glGetError());
    }

    /* mipmap levels 1..4 (9x7, 4x3, 2x1, 1x1), each with its own random
     * blocks; levels 1 and 2 are drawn minified so that exactly that level
     * is sampled, texel for pixel (scale 2, and 4.5 x 4.7) */
    {
        static unsigned char mip[5][BW * BH * 8];
        static const int mw[5] = { TW, 9, 4, 2, 1 }, mh[5] = { TH, 7, 3, 1, 1 };
        GLenum err = 0;
        for (level = 1; level <= 4; level++) {
            int nb = ((mw[level] + 3) / 4) * ((mh[level] + 3) / 4), i;
            for (i = 0; i < nb; i++) random_block(mip[level] + i * 8);
            glCompressedTexImage2D(GL_TEXTURE_2D, level, GL_ETC1_RGB8_OES, mw[level], mh[level],
                                   0, nb * 8, mip[level]);
            err |= glGetError();
        }
        printf("%s: mipmap levels 1-4: error %#x\n", api, err);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        /* (ES 1.1 only: swrast's shader sampling works out the mipmap
         * level from the fragment program's derivatives, which don't give
         * exactly these levels) */
        for (level = 1; level <= 2 && !es2; level++) {
            w = mw[level]; h = mh[level];
            draw_texels(es2, w, h, px);
            bad = compare(mip[level], w, h, px);
            printf("%s: level %d (%dx%d) sampled: %s (%d differ)\n", api, level, w, h,
                   bad ? "FAIL" : "ok", bad);
        }
    }
    glDeleteTextures(1, &t);
    OSMesaDestroyContext(c);
}

/* A perspective, bilinear draw of a 16x16 ETC1 texture (a power of two,
 * as the fast textured triangles need) against the same texels loaded as
 * GL_RGB: the same picture, bit for bit. */
static void same_as_rgb(void)
{
    static unsigned char a[VW * VH * 4], b[VW * VH * 4];
    static unsigned char blocks[4 * 4 * 8], rgb[16 * 16 * 3];
    static const GLfloat v[] = { -0.9f, -0.9f, -1.0f, 0, 0,  0.9f, -0.9f, -1.0f, 3, 0,
                                 -0.5f, 0.9f, -3.0f, 0, 2,   0.5f, 0.9f, -3.0f, 3, 2 };
    OSMesaContext c = make(0x1001);
    int pass, x, y, i, diff = 0;
    GLuint t;

    for (i = 0; i < 16; i++)
        random_block(blocks + i * 8);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            image_texel(blocks, 4, x, y, rgb + (y * 16 + x) * 3);
    for (pass = 0; pass < 2; pass++) {
        unsigned char *out = pass ? b : a;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (pass == 0)
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 16, 16, 0, sizeof blocks, blocks);
        else
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 16, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glViewport(0, 0, VW, VH);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustumf(-1, 1, -0.75f, 0.75f, 1, 10);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(3, GL_FLOAT, 20, v);
        glTexCoordPointer(2, GL_FLOAT, 20, v + 3);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glReadPixels(0, 0, VW, VH, GL_RGBA, GL_UNSIGNED_BYTE, out);
        glDeleteTextures(1, &t);
    }
    for (i = 0, x = 0; i < VW * VH * 4; i++) {
        diff += a[i] != b[i];
        x += a[i] != 0 && (i & 3) != 3;       /* something was drawn */
    }
    printf("ES 1.1: perspective bilinear ETC1 = GL_RGB: %s (%d bytes differ)\n",
           diff || !x ? "FAIL" : "ok", diff);
    OSMesaDestroyContext(c);
}

int main(void)
{
    int i;
    for (i = 0; i < BW * BH; i++)
        random_block(tex + i * 8);
    decode_cases(1);
    rng = 12345;
    for (i = 0; i < BW * BH; i++)
        random_block(tex + i * 8);
    decode_cases(0);
    same_as_rgb();
    return 0;
}
