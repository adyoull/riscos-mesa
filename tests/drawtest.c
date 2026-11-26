/*
 * drawtest.c - what a draw call costs the CPU (the GL driver's own work),
 * apart from the GPU's: for the GPU library on a Pi 4, or any renderer.
 *
 * An off-screen OSMesa context (RISC3D/the GPU library when there, else
 * software) at a small size, so drawing and reading back cost little.
 * Each mode issues N draws a frame of a small quad, then glFinish:
 *   vbo        glDrawArrays from one VBO, nothing changed between draws
 *   uniform    + a glUniform4f before each draw (GLSL program)
 *   texture    + glBindTexture, one of 8 textures in turn
 *   state      + glEnable/glDisable(GL_BLEND) and glBlendFunc each draw
 *   program    + glUseProgram, one of 4 programs in turn
 *   immediate  glBegin/glColor/glVertex x4/glEnd, fixed function
 *   clientarr  glDrawArrays from client memory (no VBO)
 * Prints, per mode: microseconds of CPU a draw (issuing them, to before
 * glFinish), and glFinish's time a frame (the GPU, and the frame's flush).
 *
 *   drawtest [draws a frame (500)] [seconds a mode (2)] [-o file] [-m mode]
 *            [-f frames a mode, in place of the seconds: for profiles]
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/osmesa.h>
#include "hrtime.h"

#define SIZE 256

static FILE *outf;
static void out(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt); vprintf(fmt, a); va_end(a);
    if (outf) { va_start(a, fmt); vfprintf(outf, fmt, a); va_end(a); fflush(outf); }
    fflush(stdout);
}

static GLuint prog[4], vbo, tex[8];
static GLint u_col[4];

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) out("shader didn't compile\n");
    return s;
}

static void setup(void)
{
    static const float quad[8] = { 0, 0, 0.05f, 0, 0.05f, 0.05f, 0, 0.05f };
    static const char *vs =
        "attribute vec2 p; uniform vec2 off; varying vec2 t;\n"
        "void main() { t = p * 20.0; gl_Position = vec4(p + off, 0.0, 1.0); }\n";
    const char *fs[4] = {
        "uniform vec4 c; uniform sampler2D s; varying vec2 t;\n"
        "void main() { gl_FragColor = c * texture2D(s, t); }\n",
        "uniform vec4 c; varying vec2 t;\nvoid main() { gl_FragColor = c * t.x; }\n",
        "uniform vec4 c; varying vec2 t;\nvoid main() { gl_FragColor = c.bgra; }\n",
        "uniform vec4 c; uniform sampler2D s; varying vec2 t;\n"
        "void main() { gl_FragColor = c + texture2D(s, t.yx); }\n" };
    unsigned char px[16 * 16 * 4];
    int i;
    GLuint v = shader(GL_VERTEX_SHADER, vs);
    for (i = 0; i < 4; i++) {
        prog[i] = glCreateProgram();
        glAttachShader(prog[i], v);
        glAttachShader(prog[i], shader(GL_FRAGMENT_SHADER, fs[i]));
        glBindAttribLocation(prog[i], 0, "p");
        glLinkProgram(prog[i]);
        u_col[i] = glGetUniformLocation(prog[i], "c");
    }
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glGenTextures(8, tex);
    for (i = 0; i < 8; i++) {
        memset(px, 40 * i, sizeof px);
        glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    glViewport(0, 0, SIZE, SIZE);
}

enum { M_VBO, M_UNIFORM, M_TEXTURE, M_STATE, M_PROGRAM, M_IMMEDIATE, M_CLIENTARR, M_COUNT };
static const char *const names[M_COUNT] = { "vbo", "uniform", "texture", "state", "program",
                                            "immediate", "clientarr" };

static void frame(int mode, int n)
{
    static const float quad[8] = { 0, 0, 0.05f, 0, 0.05f, 0.05f, 0, 0.05f };
    int i;
    glClear(GL_COLOR_BUFFER_BIT);
    if (mode == M_IMMEDIATE) {
        glUseProgram(0);
        glDisable(GL_TEXTURE_2D);
        for (i = 0; i < n; i++) {
            float x = (i % 37) / 20.0f - 1, y = (i / 37 % 37) / 20.0f - 1;
            glBegin(GL_QUADS);
            glColor3f((i & 7) / 7.0f, 0.5f, 0.2f);
            glVertex2f(x, y); glVertex2f(x + 0.05f, y); glVertex2f(x + 0.05f, y + 0.05f); glVertex2f(x, y + 0.05f);
            glEnd();
        }
        return;
    }
    glUseProgram(prog[0]);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glEnableVertexAttribArray(0);
    if (mode == M_CLIENTARR) {
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    }
    for (i = 0; i < n; i++) {
        switch (mode) {
        case M_UNIFORM: glUniform4f(u_col[0], (i & 7) / 7.0f, 0.5f, 0.2f, 1); break;
        case M_TEXTURE: glBindTexture(GL_TEXTURE_2D, tex[i & 7]); break;
        case M_STATE:
            if (i & 1) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
            else glDisable(GL_BLEND);
            break;
        case M_PROGRAM: glUseProgram(prog[i & 3]); break;
        }
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    }
    glDisable(GL_BLEND);
}

int main(int argc, char **argv)
{
    int n = 500, i, only = -1, mode, fixed = 0;
    double secs = 2;
    void *buf = malloc(SIZE * SIZE * 4);
    OSMesaContext ctx;
    double t_start = hr_seconds(), t_ctx, t_setup, t_first = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) outf = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "-f") && i + 1 < argc) fixed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
            const char *m = argv[++i];
            for (mode = 0; mode < M_COUNT; mode++) if (!strcmp(m, names[mode])) only = mode;
        } else if (n == 500 && atoi(argv[i]) > 0 && i == 1) n = atoi(argv[i]);
        else if (atof(argv[i]) > 0) secs = atof(argv[i]);
    }
    ctx = OSMesaCreateContextExt(OSMESA_RGBA, 0, 0, 0, NULL);
    if (!ctx || !buf || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, SIZE, SIZE)) {
        out("no context\n");
        return 1;
    }
    t_ctx = hr_seconds() - t_start;
    out("drawtest: %s / %s, %d draws a frame, %.1f s a mode\n", (const char *) glGetString(GL_RENDERER),
        (const char *) glGetString(GL_VERSION), n, secs);
    t_setup = hr_seconds();
    setup();
    t_setup = hr_seconds() - t_setup;
    for (mode = 0; mode < M_COUNT; mode++) {
        double t0, t, issue = 0, fin = 0, start;
        int frames = 0;
        if (only >= 0 && mode != only) continue;
        start = hr_seconds();
        frame(mode, n); glFinish();            /* (first frame: compiles and uploads, not counted) */
        t_first += hr_seconds() - start;
        start = hr_seconds();
        do {
            t0 = hr_seconds();
            frame(mode, n);
            t = hr_seconds();
            issue += t - t0;
            glFinish();
            fin += hr_seconds() - t;
            frames++;
        } while (fixed ? frames < fixed : hr_seconds() - start < secs);
        out("%-10s %7.2f us a draw (CPU)  %7.2f ms a frame in glFinish  %6.1f frames a second\n", names[mode],
            1e6 * issue / frames / n, 1e3 * fin / frames, frames / (hr_seconds() - start));
    }
    out("start-up: context %.1f ms, 4 programs and 8 textures %.1f ms, first frames %.1f ms\n",
        1e3 * t_ctx, 1e3 * t_setup, 1e3 * t_first);
    if (glGetError()) out("GL error\n");
    OSMesaDestroyContext(ctx);
    return 0;
}
