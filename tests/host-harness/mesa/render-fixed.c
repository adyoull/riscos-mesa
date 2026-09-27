/*
 * render-fixed.c - riscos-mesa rendering check: fixed-function GL.
 *
 * Renders a set of scenes into an OSMesa buffer and prints one line per
 * case, "<case> <hash of the image>", so two builds of libOSMesa can be
 * compared exactly (see run.sh).  Cases:
 *   - every depth function x lighting on/off x smooth/flat x depth writes
 *     on/off x filled/line polygons, for the depth/stencil sizes given
 *     on the command line (run.sh uses 16/0, 24/0, 24/8, 32/0);
 *   - 14 texture formats x 4 filters x 3 wrap modes x 4 texture
 *     environment modes (with blending).
 * Usage: render-fixed DEPTHBITS STENCILBITS
 * GOLD_DUMP=dir also writes each image as dir/<case>.raw (RGBA, 320x240).
 * Part of riscos-mesa, MIT licence.
 */
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define W 320
#define H 240
static unsigned char *buf;
static unsigned hash(void) {
    unsigned h = 2166136261u;
    size_t i;
    for (i = 0; i < (size_t)W * H * 4; i++) {
        h ^= buf[i];
        h *= 16777619u;
    }
    return h;
}
static void view(void) {
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.33, 1.33, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -4.5f);
}
static void sphere(float r, int sl, int st) {
    int a, b;
    for (a = 0; a < st; a++) {
        glBegin(GL_TRIANGLE_STRIP);
        for (b = 0; b <= sl; b++) {
            int k;
            for (k = 0; k < 2; k++) {
                double th = M_PI * (a + k) / st, ph = 2 * M_PI * b / sl;
                float x = sin(th) * cos(ph), y = cos(th), z = sin(th) * sin(ph);
                glNormal3f(x, y, z);
                glColor3f(.5 + .5 * x, .5 + .5 * y, .5 + .5 * z);
                glTexCoord2f(b * 2.0f / sl, (a + k) * 2.0f / st);
                glVertex3f(r * x, r * y, r * z);
            }
        }
        glEnd();
    }
}
static const GLenum funcs[] = {GL_LESS,  GL_LEQUAL,   GL_GREATER, GL_GEQUAL,
                               GL_EQUAL, GL_NOTEQUAL, GL_ALWAYS,  GL_NEVER};
static void scene(int f, int lit, int flat, int mask, int lines) {
    glClearColor(.1, .2, .3, 1);
    glClearDepth(f == 2 || f == 3 ? 0.0 : 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    view();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glShadeModel(flat ? GL_FLAT : GL_SMOOTH);
    if (lit) {
        static float l[4] = {2, 3, 4, 0};
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
        glEnable(GL_COLOR_MATERIAL);
        glLightfv(GL_LIGHT0, GL_POSITION, l);
    } else
        glDisable(GL_LIGHTING);
    if (lines)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDepthFunc(funcs[f]);
    glDepthMask(GL_TRUE);
    glPushMatrix();
    glTranslatef(-.4, 0, 0);
    sphere(1.1, 24, 16);
    glPopMatrix();
    glDepthMask(mask ? GL_TRUE : GL_FALSE);
    glPushMatrix();
    glTranslatef(.4, .1, .2);
    glRotatef(30, 1, 1, 0);
    sphere(1.0, 20, 14);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(.0, -.2, .5);
    sphere(0.6, 16, 10);
    glPopMatrix();
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glFinish();
}
static unsigned char img[64 * 64 * 4];
static void texcase(GLenum ifmt, GLenum fmt, int minf, GLenum wrap, int env) {
    int x, y;
    GLuint t;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            unsigned char *p = img + (y * 64 + x) * 4;
            p[0] = x * 4;
            p[1] = y * 4;
            p[2] = ((x ^ y) & 8) ? 250 : 10;
            p[3] = (x * y) & 255;
        }
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minf);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    minf == GL_NEAREST ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    if (minf != GL_NEAREST && minf != GL_LINEAR)
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, 64, 64, 0, fmt, GL_UNSIGNED_BYTE, img);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, env);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(.3, .2, .1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    view();
    glRotatef(50, 1, 0, 0);
    glColor4f(.9, .8, .7, .9);
    glBegin(GL_QUADS);
    glTexCoord2f(-1, -1);
    glVertex3f(-2, -2, 0);
    glTexCoord2f(3, -1);
    glVertex3f(2, -2, 0);
    glTexCoord2f(3, 3);
    glVertex3f(2, 2, 0);
    glTexCoord2f(-1, 3);
    glVertex3f(-2, 2, 0);
    glEnd();
    glPushMatrix();
    glTranslatef(0, 0, .5);
    sphere(.7, 16, 10);
    glPopMatrix();
    glFinish();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glDeleteTextures(1, &t);
}
int main(int argc, char **argv) {
    int db, sb;
    if (argc < 3) {
        fprintf(stderr, "usage: render-fixed DEPTHBITS STENCILBITS\n");
        return 1;
    }
    db = atoi(argv[1]);
    sb = atoi(argv[2]);
    int f, l, fl, m, li;
    OSMesaContext c = OSMesaCreateContextExt(OSMESA_RGBA, db, sb, 0, NULL);
    buf = malloc(W * H * 4);
    OSMesaMakeCurrent(c, buf, GL_UNSIGNED_BYTE, W, H);
    for (f = 0; f < 8; f++)
        for (l = 0; l < 2; l++)
            for (fl = 0; fl < 2; fl++)
                for (m = 0; m < 2; m++)
                    for (li = 0; li < 2; li++) {
                        scene(f, l, fl, m, li);
                        {
                            char nm[64];
                            sprintf(nm, "d%d_%d_f%d_l%d_fl%d_m%d_li%d", db, sb, f, l, fl, m, li);
                            if (getenv("GOLD_DUMP")) {
                                char pn[128];
                                FILE *o;
                                sprintf(pn, "%s/%s.raw", getenv("GOLD_DUMP"), nm);
                                o = fopen(pn, "wb");
                                fwrite(buf, 1, W * H * 4, o);
                                fclose(o);
                            }
                        }
                        printf("d%d/%d f%d l%d fl%d m%d li%d %08x\n", db, sb, f, l, fl, m, li,
                               hash());
                    }
    {
        static const GLenum fm[][2] = {{GL_RGBA, GL_RGBA},
                                       {GL_RGB, GL_RGB},
                                       {GL_RGBA, GL_BGRA},
                                       {GL_RGB8, GL_RGBA},
                                       {GL_LUMINANCE, GL_LUMINANCE},
                                       {GL_ALPHA, GL_ALPHA},
                                       {GL_LUMINANCE_ALPHA, GL_LUMINANCE_ALPHA},
                                       {GL_INTENSITY, GL_LUMINANCE},
                                       {GL_RED, GL_RED},
                                       {GL_RG, GL_RG},
                                       {GL_RGBA8, GL_BGRA},
                                       {GL_RGB, GL_BGR},
                                       {GL_LUMINANCE8, GL_RED},
                                       {GL_ALPHA8, GL_RED}};
        static const GLenum mf[] = {GL_NEAREST, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR,
                                    GL_NEAREST_MIPMAP_NEAREST};
        static const GLenum wr[] = {GL_REPEAT, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT};
        static const GLenum ev[] = {GL_MODULATE, GL_REPLACE, GL_DECAL, GL_BLEND};
        int a, b, w, e;
        for (a = 0; a < 14; a++)
            for (b = 0; b < 4; b++)
                for (w = 0; w < 3; w++)
                    for (e = 0; e < 4; e++) {
                        texcase(fm[a][0], fm[a][1], mf[b], wr[w], ev[e]);
                        if (getenv("GOLD_DUMP")) {
                            char pn[128];
                            FILE *o;
                            sprintf(pn, "%s/tex_%d_%d_%d_%d.raw", getenv("GOLD_DUMP"), a, b, w, e);
                            o = fopen(pn, "wb");
                            fwrite(buf, 1, W * H * 4, o);
                            fclose(o);
                        }
                        printf("tex %d %d %d %d %08x err%x\n", a, b, w, e, hash(), glGetError());
                    }
    }
    return 0;
}
