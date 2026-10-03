/*
 * render-fog.c - riscos-mesa rendering check: fog on 8-bit colour spans.
 *
 * Draws smooth-shaded, untextured scenes (a floor going into the
 * distance and walls at fixed depths, with colours from 0 to 255 and
 * alpha 0.5) once without fog, then with fog, and checks every fogged
 * pixel against fog worked out from first principles:
 *     expected = f * unfogged + (1 - f) * fog colour,
 * with f from the pixel's eye distance, which is read back from the
 * depth buffer. Alpha must be unchanged.
 *
 * Cases: GL_LINEAR, GL_EXP and GL_EXP2 fog, with the fog hint at
 * GL_NICEST (pixel fog) and GL_DONT_CARE (fog worked out per vertex and
 * interpolated across the span: exact for GL_LINEAR, whose factor is
 * linear in eye distance, and for walls at one depth with any mode).
 * Each prints "<case>: ok", or "<case>: FAIL ..." when a pixel is out by
 * more than 3 in a channel or the average error is over 1. Mesa's own
 * float code is out by up to 3 (mean 0.83) on the floor with vertex fog
 * (the reference works from depth, rounded) and up to 1 elsewhere;
 * riscos-mesa's integer code by the same. The errors go to stderr with
 * FOG_VERBOSE=1.
 * Part of riscos-mesa, MIT licence.
 */
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define W 203                   /* odd, so spans of every length occur */
#define H 151
#define ZN 2.0
#define ZF 60.0
static unsigned char *buf;

static void view(void)
{
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.33, 1.33, -1, 1, ZN, ZF);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* scene 0: a floor from 2.5 to 55 away and a far wall; scene 1: walls
 * facing the eye at 4 depths (one fog factor each) */
static void scene(int which)
{
    int k, z;
    if (which == 0) {
        for (z = 0; z < 6; z++) {
            glBegin(GL_QUAD_STRIP);
            for (k = 0; k <= 6; k++) {
                float x = -40 + 13.3f * k, z0 = -2.5f - 8.75f * z, z1 = z0 - 8.75f;
                glColor4f((k & 1) ? 1.0f : 0.0f, 0.2f * z, (k * 37 % 7) / 6.0f, 0.5f);
                glVertex3f(x, -1.5f, z0);
                glColor4f(0.5f, (k & 2) ? 1.0f : 0.0f, 1.0f - 0.15f * z, 0.5f);
                glVertex3f(x, -1.5f, z1);
            }
            glEnd();
        }
        glBegin(GL_QUADS);       /* far wall, above the floor */
        glColor4f(1, 1, 1, 0.5f); glVertex3f(-70, -1.5f, -55);
        glColor4f(0, 0, 0, 0.5f); glVertex3f(70, -1.5f, -55);
        glColor4f(1, 0, 1, 0.5f); glVertex3f(70, 40, -55);
        glColor4f(0, 1, 0, 0.5f); glVertex3f(-70, 40, -55);
        glEnd();
    } else {
        static const float d[4] = {3, 9, 20, 45};
        for (k = 0; k < 4; k++) {
            float s = d[k] * 0.66f, x0 = -s + (k & 1) * s, y0 = -s / 1.33f + (k >> 1) * s / 1.33f;
            glBegin(GL_QUADS);
            glColor4f(1, 0, 0, 0.5f); glVertex3f(x0, y0, -d[k]);
            glColor4f(0, 1, 0, 0.5f); glVertex3f(x0 + s, y0, -d[k]);
            glColor4f(0, 0, 1, 0.5f); glVertex3f(x0 + s, y0 + s / 1.33f, -d[k]);
            glColor4f(1, 1, 1, 0.5f); glVertex3f(x0, y0 + s / 1.33f, -d[k]);
            glEnd();
        }
    }
}

static float factor(GLenum mode, float c)
{
    float f;
    if (mode == GL_LINEAR) f = (70.0f - c) / (70.0f - 4.0f);
    else if (mode == GL_EXP) f = expf(-0.06f * c);
    else f = expf(-(0.05f * 0.05f) * c * c);
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

static int check(const char *name, GLenum mode, GLenum hint, int which, const float fogc[4])
{
    static unsigned char base[W * H * 4];
    static float depth[W * H];
    double sum = 0;
    int i, worst = 0, n = 0, alpha_bad = 0;

    view();
    glClearColor(0, 0, 0, 0);
    glDisable(GL_FOG);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    scene(which);
    glFinish();
    memcpy(base, buf, sizeof base);

    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, mode);
    glFogf(GL_FOG_START, 4.0f);
    glFogf(GL_FOG_END, 70.0f);   /* beyond the scene: per-vertex factors aren't clamped */
    glFogf(GL_FOG_DENSITY, mode == GL_EXP ? 0.06f : 0.05f);
    glFogfv(GL_FOG_COLOR, fogc);
    glHint(GL_FOG_HINT, hint);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    scene(which);
    glFinish();
    glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_FLOAT, depth);

    for (i = 0; i < W * H; i++) {
        const unsigned char *b = base + i * 4, *p = buf + i * 4;
        float zn, ze, f;
        int c;
        if (depth[i] >= 1.0f)
            continue;                         /* nothing drawn here */
        zn = 2.0f * depth[i] - 1.0f;          /* eye distance from depth */
        ze = (float)(2.0 * ZN * ZF / ((ZF + ZN) - zn * (ZF - ZN)));
        f = factor(mode, ze);
        for (c = 0; c < 3; c++) {
            float want = f * b[c] + (1 - f) * fogc[c] * 255.0f;
            int d = (int)fabsf(p[c] - want);
            sum += fabsf(p[c] - want);
            if (d > worst) worst = d;
        }
        if (p[3] != b[3]) alpha_bad++;
        n++;
    }
    if (getenv("FOG_VERBOSE"))
        fprintf(stderr, "%s: %d pixels, worst %d, mean %.3f\n", name, n, worst, sum / (n * 3.0));
    if (n < W * H / 4 || worst > 3 || sum / (n * 3.0) > 1.0 || alpha_bad) {
        printf("%s: FAIL (%d pixels, worst %d, mean %.3f, alpha changed %d)\n",
               name, n, worst, sum / (n * 3.0), alpha_bad);
        return 1;
    }
    printf("%s: ok\n", name);
    return 0;
}

int main(void)
{
    static const float grey[4] = {0.75f, 0.8f, 0.85f, 1}, odd[4] = {1, 0, 0.5f, 1};
    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 0, 0, NULL);
    int bad = 0;
    buf = malloc(W * H * 4);
    if (!ctx || !buf || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
        printf("OSMesa setup failed\n");
        return 1;
    }
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    bad |= check("linear, pixel fog, floor", GL_LINEAR, GL_NICEST, 0, grey);
    bad |= check("linear, vertex fog, floor", GL_LINEAR, GL_DONT_CARE, 0, grey);
    bad |= check("linear, vertex fog, floor, red fog", GL_LINEAR, GL_FASTEST, 0, odd);
    bad |= check("exp, pixel fog, floor", GL_EXP, GL_NICEST, 0, grey);
    bad |= check("exp2, pixel fog, floor", GL_EXP2, GL_NICEST, 0, odd);
    bad |= check("exp, vertex fog, walls", GL_EXP, GL_DONT_CARE, 1, grey);
    bad |= check("exp2, vertex fog, walls", GL_EXP2, GL_DONT_CARE, 1, odd);
    bad |= check("linear, vertex fog, walls", GL_LINEAR, GL_DONT_CARE, 1, odd);
    OSMesaDestroyContext(ctx);
    (void)bad;   /* run.sh compares the lines with expected/render-fog.txt */
    return 0;
}
