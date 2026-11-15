/*
 * v3dtri.c - the first triangle through Mesa's v3d driver and V3DRender
 * on native RISC OS (riscos-mesa's v3d prototype).
 *
 * Opens V3DRender, starts Mesa's v3d driver on it, makes an OpenGL
 * context drawing into a 256x256 frame, clears it and draws a triangle
 * and a smooth-shaded quad, reads the frame back and checks some pixels.
 * It writes what it found to the file named on the command line (or
 * "Result") and the picture as a sprite file "Picture" (32bpp, for Paint).
 *
 *   v3dtri [RESULT-FILE [FRAMES]]     (FRAMES: the timed frames, default 100)
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <GL/gl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "v3dm.h"

#define W 256
#define H 256

static FILE *out;
static int failures;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (out) {
        va_start(ap, fmt);
        vfprintf(out, fmt, ap);
        va_end(ap);
        fflush(out);
    }
}

static void check(int ok, const char *what, unsigned int got, unsigned int want)
{
    failures += !ok;
    say("%s %06x %06x  %s\n", ok ? "ok  " : "FAIL", got, want, what);
}

static unsigned int px(const unsigned char *f, int x, int y)
{
    const unsigned char *p = f + (y * W + x) * 4;
    return (unsigned int) p[0] << 16 | (unsigned int) p[1] << 8 | p[2];
}

/* a 32bpp sprite file (mode word: 32bpp, 90 dpi), rows top first */
static void save_sprite(const char *name, const unsigned char *f)
{
    FILE *s = fopen(name, "wb");
    int x, y;
    unsigned int hdr[3 + 11];
    if (!s)
        return;
    hdr[0] = 1;                         /* one sprite */
    hdr[1] = 16;                        /* first sprite at offset 16 (from the area's start, less 4) */
    hdr[2] = 16 + 44 + W * H * 4;       /* free space */
    hdr[3] = 44 + W * H * 4;            /* this sprite's size */
    memset(&hdr[4], 0, 12);
    memcpy(&hdr[4], "picture", 7);
    hdr[7] = W - 1;                     /* width in words, less 1 */
    hdr[8] = H - 1;                     /* height in rows, less 1 */
    hdr[9] = 0;                         /* first bit used */
    hdr[10] = 31;                       /* last bit used */
    hdr[11] = 44;                       /* image offset */
    hdr[12] = 44;                       /* mask offset (none) */
    hdr[13] = 1 | (90 << 1) | (90 << 14) | (6u << 27);
    fwrite(hdr, 4, 14, s);
    for (y = H - 1; y >= 0; y--)        /* GL's rows are bottom first */
        for (x = 0; x < W; x++) {
            const unsigned char *p = f + (y * W + x) * 4;
            unsigned char b[4] = { p[0], p[1], p[2], 0 };    /* &BBGGRR: R in the low byte */
            fwrite(b, 1, 4, s);
        }
    fclose(s);
}

int main(int argc, char **argv)
{
    static unsigned char frame[W * H * 4];
    v3dm_context *c;
    clock_t t0;
    int i;

    int frames = argc > 2 ? atoi(argv[2]) : 100;
    out = fopen(argc > 1 ? argv[1] : "Result", "w");
    say("v3dtri: riscos-mesa's v3d prototype (Mesa 26.1.6's v3d driver on V3DRender)\n");

    say("     starting Mesa's v3d driver on V3DRender%s\n", getenv("V3DM$Fake") ? " (V3DM$Fake: no GPU)" : "");
    t0 = clock();
    c = v3dm_create(V3DM_API_GL, 2, 1, 1);
    if (!c) {
        say("FAIL no context: %s\n", v3dm_error());
        return 1;
    }
    say("ok   context in %d cs: %s\n", (int) ((clock() - t0) * 100 / CLOCKS_PER_SEC), v3dm_renderer());
    say("     making it current on a %dx%d frame\n", W, H);
    if (!v3dm_make_current(c, W, H)) {
        say("FAIL make current\n");
        return 1;
    }
    say("GL_VENDOR   %s\n", (const char *) glGetString(GL_VENDOR));
    say("GL_RENDERER %s\n", (const char *) glGetString(GL_RENDERER));
    say("GL_VERSION  %s\n", (const char *) glGetString(GL_VERSION));

    say("     clearing\n");
    /* 1: a clear */
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glFinish();
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, frame);
    check(px(frame, 10, 10) == 0x0000FF && px(frame, 200, 200) == 0x0000FF,
          "the GPU cleared the frame to blue", px(frame, 10, 10), 0x0000FF);

    say("     drawing (Mesa compiles its shaders now)\n");
    /* 2: a red triangle (bottom left half) and a smooth quad in front
       (depth tested), as the software tests draw */
    t0 = clock();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0);
    glVertex3f(-1, -1, 0.5f); glVertex3f(1, -1, 0.5f); glVertex3f(-1, 1, 0.5f);
    glEnd();
    glBegin(GL_QUADS);
    glColor3f(0, 1, 0); glVertex3f(-0.25f, -0.25f, 0);
    glColor3f(0, 1, 0); glVertex3f(0.25f, -0.25f, 0);
    glColor3f(1, 1, 1); glVertex3f(0.25f, 0.25f, 0);
    glColor3f(1, 1, 1); glVertex3f(-0.25f, 0.25f, 0);
    glEnd();
    glFinish();
    say("     first draw (shaders compiled) in %d cs\n", (int) ((clock() - t0) * 100 / CLOCKS_PER_SEC));
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, frame);
    check(px(frame, 20, 20) == 0xFF0000, "red triangle bottom left", px(frame, 20, 20), 0xFF0000);
    check(px(frame, 236, 236) == 0x0000FF, "blue top right", px(frame, 236, 236), 0x0000FF);
    check(px(frame, 128, 100) == 0x00FF00 || ((px(frame, 128, 100) >> 8) & 0xFF) > 0xC0,
          "green quad in front, centre bottom", px(frame, 128, 100), 0x00FF00);
    save_sprite("Picture", frame);
    say("     picture saved as Picture (sprite)\n");

    /* 3: speed: frames of the same scene */
    t0 = clock();
    for (i = 0; i < frames; i++) {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glColor3f(1, 0, 0);
        glVertex3f(-1, -1, 0.5f); glVertex3f(1, -1, 0.5f); glVertex3f(-1, 1, 0.5f);
        glEnd();
        glFinish();
    }
    say("     %d frames (clear, triangle, glFinish) in %d cs\n", frames, (int) ((clock() - t0) * 100 / CLOCKS_PER_SEC));
    check(glGetError() == GL_NO_ERROR, "no GL error", 0, 0);

    v3dm_destroy(c);
    v3dm_shutdown();
    say("%s: %d failures\n", failures ? "FAIL" : "ALL PASS", failures);
    if (out)
        fclose(out);
    return failures != 0;
}
