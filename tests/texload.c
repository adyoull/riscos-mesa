/*
 * texload.c - textures loaded the way plib's ssgMakeMipMaps does (TORCS's
 * track and cars): the old internal formats 3 and 4, each mipmap level
 * uploaded with its own glTexImage2D, GL_LINEAR_MIPMAP_LINEAR, then drawn
 * from client arrays with glDrawElements, as plib's leaves are.
 *
 * Each texture is one colour; a quad drawn with it must come out in that
 * colour. A texture the renderer thinks incomplete (a level missing or
 * wrong) is drawn as if there were none: the quad comes out white.
 * Prints one line per check and writes them to the file named on the
 * command line as well.
 *
 *   texload [RESULTS-FILE]
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#define GL_GLEXT_PROTOTYPES
#include <GL/freeglut.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *out;
static int failures;

static void say(const char *fmt, const char *a, unsigned int b, unsigned int c)
{
    printf(fmt, a, b, c);
    if (out)
        fprintf(out, fmt, a, b, c);
}

static void check(int ok, const char *what, unsigned int got, unsigned int want)
{
    failures += !ok;
    say("%s %06x %06x\n", ok ? "ok  " : "FAIL", got, want);
    say("     %s\n", what, 0, 0);
}

/* plib's ssgMakeMipMaps: halve until 1x1, one glTexImage2D per level,
   internal format = the number of components */
static GLuint load(int w, int h, int z, unsigned int rgb, int levels)
{
    static const GLenum fmt[] = { 0, GL_LUMINANCE, GL_LUMINANCE_ALPHA, GL_RGB, GL_RGBA };
    GLubyte *px = malloc((size_t) w * h * z + 4);
    GLuint t;
    int i, l;
    for (i = 0; i < w * h; i++) {
        GLubyte c[4] = { (GLubyte) (rgb >> 16), (GLubyte) (rgb >> 8), (GLubyte) rgb, 255 };
        memcpy(px + i * z, c, (size_t) z);
    }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    /* plib first asks whether the size fits (a proxy texture), and TORCS
       queries the texture's state, as its log shows (glGetTexParameteriv,
       glGetTexLevelParameteriv, glHint per texture) */
    {
        GLint pw = 0, ws = 0;
        glTexImage2D(GL_PROXY_TEXTURE_2D, 0, z, w, h, 0, fmt[z], GL_UNSIGNED_BYTE, NULL);
        glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &pw);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &ws);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &ws);
        glGetError();
        if (pw != w)
            say("note: proxy width %s%x for %x\n", "", (unsigned int) pw, (unsigned int) w);
    }
    glHint(GL_TEXTURE_COMPRESSION_HINT, GL_NICEST);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, levels ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    for (l = 0; ; l++) {
        int lw = w >> l, lh = h >> l;
        if (lw < 1) lw = 1;
        if (lh < 1) lh = 1;
        /* the image for each level is the same colour: the first w*h
           pixels serve for every level */
        glTexImage2D(GL_TEXTURE_2D, l, z, lw, lh, 0, fmt[z], GL_UNSIGNED_BYTE, px);
        if (!levels || (lw == 1 && lh == 1))
            break;
    }
    free(px);
    return t;
}

/* a quad of the window's size, or a small one (the smallest levels),
   from client arrays with glDrawElements */
static unsigned int draw(GLuint t, float size)
{
    GLfloat v[8] = { -size, -size, size, -size, size, size, -size, size };
    static const GLfloat tc[] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    static const GLushort idx[] = { 0, 1, 2, 3 };
    GLubyte p[4];
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, t);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, idx);
    glPopClientAttrib();
    glDisable(GL_TEXTURE_2D);
    glReadPixels(64, 64, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return (unsigned int) p[0] << 16 | (unsigned int) p[1] << 8 | p[2];
}

static int near(unsigned int a, unsigned int b)
{
    int i;
    for (i = 0; i < 24; i += 8) {
        int d = (int) ((a >> i) & 255) - (int) ((b >> i) & 255);
        if (d < -8 || d > 8)
            return 0;
    }
    return 1;
}

static void one(const char *what, int w, int h, int z, unsigned int rgb, int levels, int align)
{
    GLuint t;
    unsigned int c;
    char line[160];
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    t = load(w, h, z, rgb, levels);
    c = draw(t, 1.0f);
    snprintf(line, sizeof line, "%s, %dx%d, format %d, %s, alignment %d: full size", what, w, h, z,
             levels ? "all levels" : "one level", align);
    check(near(c, rgb), line, c, rgb);
    if (levels) {
        c = draw(t, 0.02f);   /* about 3 pixels: the small levels */
        snprintf(line, sizeof line, "%s, %dx%d, format %d: drawn small (the last levels)", what, w, h, z);
        check(near(c, rgb), line, c, rgb);
    }
    glDeleteTextures(1, &t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

/* Arrays whose used vertices don't start at 0, as plib's strips draw
   from one shared set of arrays: a 2x2 texture (red, green / blue,
   yellow), drawn NEAREST over the window from vertices 4 to 7 (0 to 3
   are other strips' and off screen). Each quarter of the window must
   show its texel. stride: 0 = separate arrays, else one interleaved
   array of that many floats per vertex. */
static void rebased(int stride)
{
    static const GLubyte tx[] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0 };
    static const unsigned int want[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };
    static const GLushort idx[] = { 4, 5, 6, 7 };
    GLfloat v[8][2], tc[8][2], il[8][5];
    GLuint t;
    int i, q;
    char line[120];
    for (i = 0; i < 8; i++) {
        float x = i < 4 ? 5.0f + i : (i == 4 || i == 7 ? -1.0f : 1.0f);
        float y = i < 4 ? 5.0f : (i < 6 ? -1.0f : 1.0f);
        float s = i < 4 ? 0.9f : (x + 1) / 2, u = i < 4 ? 0.9f : (y + 1) / 2;
        v[i][0] = x; v[i][1] = y; tc[i][0] = s; tc[i][1] = u;
        il[i][0] = s; il[i][1] = u; il[i][2] = x; il[i][3] = y; il[i][4] = 0;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, tx);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    if (stride) {
        glTexCoordPointer(2, GL_FLOAT, stride * (GLsizei) sizeof(GLfloat), &il[0][0]);
        glVertexPointer(2, GL_FLOAT, stride * (GLsizei) sizeof(GLfloat), &il[0][2]);
    } else {
        glVertexPointer(2, GL_FLOAT, 0, v);
        glTexCoordPointer(2, GL_FLOAT, 0, tc);
    }
    glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, idx);
    glPopClientAttrib();
    glDisable(GL_TEXTURE_2D);
    for (q = 0; q < 4; q++) {
        GLubyte p[4];
        unsigned int c;
        glReadPixels(q & 1 ? 96 : 32, q & 2 ? 96 : 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        c = (unsigned int) p[0] << 16 | (unsigned int) p[1] << 8 | p[2];
        snprintf(line, sizeof line, "vertices 4-7 of shared arrays (%s): quarter %d shows its texel",
                 stride ? "interleaved" : "separate", q);
        check(c == want[q], line, c, want[q]);
    }
    glDeleteTextures(1, &t);
}

/* State that glPopClientAttrib and glPopAttrib must put back, as TORCS's
   grVtxTable and plib's leaves leave it: each leaf sets units 1 and 2's
   texture-coordinate arrays inside glPushClientAttrib, and the next leaf
   sets only unit 0's. The 2x2 texture (red, green / blue, yellow) on unit
   0 must then show the right texel in each quarter. */
static void popped(void)
{
    static const GLubyte tx[] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0 };
    static const unsigned int want[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };
    static const GLushort idx[] = { 0, 1, 2, 3 };
    static const GLfloat v[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
    static const GLfloat tc[] = { 0.25f, 0.25f, 0.75f, 0.25f, 0.75f, 0.75f, 0.25f, 0.75f };
    static const GLfloat junk[] = { 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f };
    GLuint t;
    GLint cu = 0, au = 0;
    int q, pass;
    char line[160];

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, tx);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    for (pass = 0; pass < 2; pass++) {
        /* a "multitextured leaf": units 1 and 2's arrays, and (pass 1)
           the server's active unit inside glPushAttrib */
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        if (pass)
            glPushAttrib(GL_TEXTURE_BIT | GL_ENABLE_BIT);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, v);
        glClientActiveTextureARB(GL_TEXTURE0_ARB);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, junk);
        glClientActiveTextureARB(GL_TEXTURE1_ARB);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, junk);
        glClientActiveTextureARB(GL_TEXTURE2_ARB);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, junk);
        if (pass)
            glActiveTextureARB(GL_TEXTURE2_ARB);
        /* left as it is, as a leaf leaves it */
        if (pass)
            glPopAttrib();
        glPopClientAttrib();

        glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE_ARB, &cu);
        glGetIntegerv(GL_ACTIVE_TEXTURE_ARB, &au);
        snprintf(line, sizeof line, "after the pops%s: client unit (first) and unit (second) back to 0",
                 pass ? " (glPopAttrib too)" : "");
        check(cu == GL_TEXTURE0_ARB && au == GL_TEXTURE0_ARB, line, (unsigned int) cu, (unsigned int) au);

        /* the next, single-textured leaf: unit 0 only, without selecting it */
        glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, t);
        glEnable(GL_TEXTURE_2D);
        glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, v);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, tc);
        glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, idx);
        glPopClientAttrib();
        glDisable(GL_TEXTURE_2D);
        for (q = 0; q < 4; q++) {
            GLubyte p[4];
            unsigned int c;
            glReadPixels(q & 1 ? 96 : 32, q & 2 ? 96 : 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
            c = (unsigned int) p[0] << 16 | (unsigned int) p[1] << 8 | p[2];
            snprintf(line, sizeof line, "next leaf after a multitextured one%s: quarter %d shows its texel",
                     pass ? " (glPopAttrib too)" : "", q);
            check(c == want[q], line, c, want[q]);
        }

        /* units 1 and 2 must be off again: a texture there would change
           the colour */
        {
            GLint on1 = 1, on2 = 1;
            glClientActiveTextureARB(GL_TEXTURE1_ARB);
            on1 = glIsEnabled(GL_TEXTURE_COORD_ARRAY);
            glClientActiveTextureARB(GL_TEXTURE2_ARB);
            on2 = glIsEnabled(GL_TEXTURE_COORD_ARRAY);
            glClientActiveTextureARB(GL_TEXTURE0_ARB);
            snprintf(line, sizeof line, "after the pops%s: units 1 and 2's coordinate arrays off",
                     pass ? " (glPopAttrib too)" : "");
            check(!on1 && !on2, line, (unsigned int) on1, (unsigned int) on2);
        }
    }
    glDeleteTextures(1, &t);
}

/* plib's leaves: vertex, normal, texture-coordinate and colour arrays all
   on (ssgVtxTable::draw_geometry), drawn from vertices 4 to 7 of shared
   arrays as a strip. The 2x2 texture must show the right texel in each
   quarter: texture coordinates taken from another array (the normals,
   the colours) smear the texture or give one colour. */
static void leaf(int lit)
{
    static const GLubyte tx[] = { 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0 };
    static const unsigned int want[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };
    static const GLushort idx[] = { 4, 5, 7, 6 };   /* a strip */
    GLfloat v[8][3], n[8][3], tc[8][2], col[8][4];
    GLuint t;
    int i, q;
    char line[160];
    for (i = 0; i < 8; i++) {
        float x = i < 4 ? 5.0f + i : (i == 4 || i == 7 ? -1.0f : 1.0f);
        float y = i < 4 ? 5.0f : (i < 6 ? -1.0f : 1.0f);
        v[i][0] = x; v[i][1] = y; v[i][2] = 0;
        n[i][0] = 0.3f; n[i][1] = 0.6f; n[i][2] = 0.74f;
        tc[i][0] = i < 4 ? 0.9f : (x + 1) / 2; tc[i][1] = i < 4 ? 0.9f : (y + 1) / 2;
        col[i][0] = col[i][1] = col[i][2] = col[i][3] = 1;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, tx);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glClear(GL_COLOR_BUFFER_BIT);
    if (lit) {
        /* full white light along the normal: colour stays 1 */
        static const GLfloat white[] = { 1, 1, 1, 1 }, black[] = { 0, 0, 0, 1 }, dir[] = { 0.3f, 0.6f, 0.74f, 0 };
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glLightfv(GL_LIGHT0, GL_POSITION, dir);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, white);
        glLightfv(GL_LIGHT0, GL_AMBIENT, black);
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, black);
        glEnable(GL_COLOR_MATERIAL);
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    }
    glEnable(GL_TEXTURE_2D);
    glPushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, v);
    glEnableClientState(GL_NORMAL_ARRAY);
    glNormalPointer(GL_FLOAT, 0, n);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_FLOAT, 0, col);
    glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, idx);
    glPopClientAttrib();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_COLOR_MATERIAL);
    glColor4f(1, 1, 1, 1);
    for (q = 0; q < 4; q++) {
        GLubyte p[4];
        unsigned int c;
        glReadPixels(q & 1 ? 96 : 32, q & 2 ? 96 : 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        c = (unsigned int) p[0] << 16 | (unsigned int) p[1] << 8 | p[2];
        snprintf(line, sizeof line, "plib leaf: vertex, normal, texcoord and colour arrays%s: quarter %d shows its texel",
                 lit ? ", lit" : "", q);
        check(near(c, want[q]), line, c, want[q]);
    }
    glDeleteTextures(1, &t);
}

/* TORCS's "Scene resolution" below 100% (its RISC OS patch 0010): the
   scene is drawn into the bottom-left part of the window, copied into a
   texture with glCopyTexSubImage2D and drawn over the whole window as one
   textured quad (GL_CLAMP_TO_EDGE, GL_LINEAR, GL_REPLACE). Four coloured
   quarters drawn small must come out as the same four quarters, full
   size. sub: glCopyTexSubImage2D into a texture made first (TORCS's way),
   else glCopyTexImage2D. */
static void scene_copy(int sub)
{
    static const unsigned int want[4] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00 };
    GLuint t;
    int q;
    char line[160];
    glViewport(0, 0, 64, 64);
    glScissor(0, 0, 64, 64);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glBegin(GL_QUADS);
    for (q = 0; q < 4; q++) {
        float x0 = q & 1 ? 0 : -1, y0 = q & 2 ? 0 : -1;
        glColor3ub((GLubyte) (want[q] >> 16), (GLubyte) (want[q] >> 8), (GLubyte) want[q]);
        glVertex2f(x0, y0); glVertex2f(x0 + 1, y0); glVertex2f(x0 + 1, y0 + 1); glVertex2f(x0, y0 + 1);
    }
    glEnd();
    glColor4f(1, 1, 1, 1);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (sub) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 64, 64);
    } else
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 64, 64, 0);
    glViewport(0, 0, 128, 128);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    for (q = 0; q < 4; q++) {
        GLubyte p[4];
        unsigned int c;
        glReadPixels(q & 1 ? 96 : 32, q & 2 ? 96 : 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
        c = (unsigned int) p[0] << 16 | (unsigned int) p[1] << 8 | p[2];
        snprintf(line, sizeof line, "scene drawn small, %s, drawn full size: quarter %d",
                 sub ? "glCopyTexSubImage2D" : "glCopyTexImage2D", q);
        check(near(c, want[q]), line, c, want[q]);
    }
    glDeleteTextures(1, &t);
}

int main(int argc, char **argv)
{
    const char *s;
    glutInit(&argc, argv);
    out = argc > 1 ? fopen(argv[1], "w") : NULL;
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
    glutInitWindowSize(128, 128);
    glutCreateWindow("texload");
    s = (const char *) glGetString(GL_RENDERER);
    say("GL_RENDERER %s\n", s ? s : "(none)", 0, 0);
    s = (const char *) glGetString(GL_VERSION);
    say("GL_VERSION  %s\n", s ? s : "(none)", 0, 0);
    glViewport(0, 0, 128, 128);
    glClearColor(0, 0, 0, 1);
    glColor4f(1, 1, 1, 1);

    one("RGBA (TORCS's png)",      512, 128, 4, 0xC03020, 1, 1);
    one("RGBA (TORCS's png)",      256, 256, 4, 0x2080C0, 1, 4);
    one("RGB (TORCS's rgb)",       512, 512, 3, 0x30A040, 1, 1);
    one("RGB, odd row length",      64,  32, 3, 0xA0A020, 1, 4);
    one("RGB, odd row length",      16,   2, 3, 0x8020A0, 1, 1);
    one("RGBA, one level",         128, 128, 4, 0x40C0C0, 0, 4);
    one("RGB, one level",          128,  64, 3, 0xC04080, 0, 1);
    rebased(0);
    rebased(5);
    popped();
    leaf(0);
    leaf(1);
    scene_copy(1);
    scene_copy(0);
    check(glGetError() == GL_NO_ERROR, "no GL error", 0, 0);

    say("%s: %x failures\n", failures ? "FAIL" : "ALL PASS", (unsigned int) failures, 0);
    if (out)
        fclose(out);
    return failures != 0;
}
