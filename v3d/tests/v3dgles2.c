/*
 * v3dgles2.c - OpenGL ES 2.0 through Mesa's v3d driver (through RISC3D) on
 * native RISC OS (riscos-mesa's v3d prototype): GLSL shaders compiled by
 * the v3d compiler, vertex buffers, a texture, uniforms, blending, and a
 * full-HD frame timed.
 *
 *   v3dgles2 [RESULT-FILE [FRAMES]]   (FRAMES: the timed 1920x1080 frames, default 100)
 *
 * It writes what it found to RESULT-FILE (default "Result") and the
 * 256x256 picture as a 32bpp sprite file "Picture".
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <GLES2/gl2.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "v3dm.h"

#define W 256
#define H 256
#define BIG_W 1920
#define BIG_H 1080

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

static unsigned int px(const unsigned char *f, int w, int x, int y)
{
    const unsigned char *p = f + (y * w + x) * 4;
    return (unsigned int) p[0] << 16 | (unsigned int) p[1] << 8 | p[2];
}

/* each of R, G, B within tol of want's */
static int near(unsigned int got, unsigned int want, int tol)
{
    int s;
    for (s = 0; s < 24; s += 8) {
        int d = (int) ((got >> s) & 0xFF) - (int) ((want >> s) & 0xFF);
        if (d < -tol || d > tol)
            return 0;
    }
    return 1;
}

/* a 32bpp sprite file (as v3dtri's), rows top first */
static void save_sprite(const char *name, const unsigned char *f)
{
    FILE *s = fopen(name, "wb");
    int x, y;
    unsigned int hdr[14];
    if (!s)
        return;
    hdr[0] = 1;
    hdr[1] = 16;
    hdr[2] = 16 + 44 + W * H * 4;
    hdr[3] = 44 + W * H * 4;
    memset(&hdr[4], 0, 12);
    memcpy(&hdr[4], "picture", 7);
    hdr[7] = W - 1;
    hdr[8] = H - 1;
    hdr[9] = 0;
    hdr[10] = 31;
    hdr[11] = 44;
    hdr[12] = 44;
    hdr[13] = 1 | (90 << 1) | (90 << 14) | (6u << 27);
    fwrite(hdr, 4, 14, s);
    for (y = H - 1; y >= 0; y--)
        for (x = 0; x < W; x++) {
            const unsigned char *p = f + (y * W + x) * 4;
            unsigned char b[4] = { p[0], p[1], p[2], 0 };
            fwrite(b, 1, 4, s);
        }
    fclose(s);
}

/* ---- shaders ---- */

static const char *vs_text =
    "attribute vec2 a_pos;\n"
    "attribute vec4 a_colour;\n"
    "attribute vec2 a_tex;\n"
    "uniform vec2 u_offset;\n"
    "varying vec4 v_colour;\n"
    "varying vec2 v_tex;\n"
    "void main() {\n"
    "    v_colour = a_colour;\n"
    "    v_tex = a_tex;\n"
    "    gl_Position = vec4(a_pos + u_offset, 0.0, 1.0);\n"
    "}\n";

/* colour only, times a tint */
static const char *fs_colour_text =
    "precision mediump float;\n"
    "uniform vec4 u_tint;\n"
    "varying vec4 v_colour;\n"
    "void main() {\n"
    "    gl_FragColor = v_colour * u_tint;\n"
    "}\n";

/* the texture, times the colour */
static const char *fs_tex_text =
    "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec4 v_colour;\n"
    "varying vec2 v_tex;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_tex, v_tex) * v_colour;\n"
    "}\n";

static GLuint compile(GLenum type, const char *text, const char *name)
{
    GLuint s = glCreateShader(type);
    GLint ok = 0;
    char log[512];
    glShaderSource(s, 1, &text, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        say("     %s's log: %s\n", name, log);
    }
    check(ok, name, ok, 1);
    return s;
}

static GLuint link(GLuint vs, GLuint fs, const char *name)
{
    GLuint p = glCreateProgram();
    GLint ok = 0;
    char log[512];
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_colour");
    glBindAttribLocation(p, 2, "a_tex");
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        say("     %s's log: %s\n", name, log);
    }
    check(ok, name, ok, 1);
    return p;
}

/* ---- the scene ----
   Three quads, two triangles each: x, y, r, g, b, a, s, t per vertex.
   0: left half, yellow (the colour program, tinted to orange)
   1: right half, white, textured with an 8x8-texel checker
   2: a half-transparent blue square over the left half, blended */
#define QUAD(x0, y0, x1, y1, r, g, b, a) \
    x0, y0, r, g, b, a, 0, 0,   x1, y0, r, g, b, a, 1, 0,   x1, y1, r, g, b, a, 1, 1, \
    x0, y0, r, g, b, a, 0, 0,   x1, y1, r, g, b, a, 1, 1,   x0, y1, r, g, b, a, 0, 1
static const GLfloat scene[] = {
    QUAD(-1.0f, -1.0f, 0.0f, 1.0f, 1, 1, 0, 1),
    QUAD(0.0f, -1.0f, 1.0f, 1.0f, 1, 1, 1, 1),
    QUAD(-0.75f, 0.25f, -0.25f, 0.75f, 0, 0, 1, 0.5f),
};

static GLuint prog_colour, prog_tex, vbo, tex;
static GLint tint_at, offset_c_at, offset_t_at, sampler_at;

static void setup(void)
{
    static unsigned char checker[64 * 64 * 4];
    GLuint vs, fsc, fst;
    int x, y;

    say("     compiling shaders (Mesa's GLSL compiler, then v3d's)\n");
    vs = compile(GL_VERTEX_SHADER, vs_text, "vertex shader compiled");
    fsc = compile(GL_FRAGMENT_SHADER, fs_colour_text, "colour fragment shader compiled");
    fst = compile(GL_FRAGMENT_SHADER, fs_tex_text, "texture fragment shader compiled");
    prog_colour = link(vs, fsc, "colour program linked");
    prog_tex = link(vs, fst, "texture program linked");
    tint_at = glGetUniformLocation(prog_colour, "u_tint");
    offset_c_at = glGetUniformLocation(prog_colour, "u_offset");
    offset_t_at = glGetUniformLocation(prog_tex, "u_offset");
    sampler_at = glGetUniformLocation(prog_tex, "u_tex");
    check(tint_at >= 0 && offset_c_at >= 0 && offset_t_at >= 0 && sampler_at >= 0,
          "uniforms found", 0, 0);

    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof scene, scene, GL_STATIC_DRAW);

    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            unsigned char v = ((x >> 3) ^ (y >> 3)) & 1 ? 0 : 255;
            unsigned char *p = checker + (y * 64 + x) * 4;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, checker);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void *) 0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void *) (2 * sizeof(GLfloat)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(GLfloat), (void *) (6 * sizeof(GLfloat)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
}

static void draw(int w, int h)
{
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);

    glUseProgram(prog_colour);
    glUniform4f(tint_at, 1.0f, 0.5f, 1.0f, 1.0f);
    glUniform2f(offset_c_at, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glUseProgram(prog_tex);
    glUniform1i(sampler_at, 0);
    glUniform2f(offset_t_at, 0, 0);
    glDrawArrays(GL_TRIANGLES, 6, 6);

    glUseProgram(prog_colour);
    glUniform4f(tint_at, 1, 1, 1, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(GL_TRIANGLES, 12, 6);
}

int main(int argc, char **argv)
{
    static unsigned char frame[W * H * 4];
    unsigned char one[4];
    v3dm_context *c;
    clock_t t0;
    int i, frames = argc > 2 ? atoi(argv[2]) : 100;

    out = fopen(argc > 1 ? argv[1] : "Result", "w");
    say("v3dgles2: OpenGL ES 2.0 on riscos-mesa's v3d prototype (Mesa 26.1.6's v3d driver, through RISC3D)\n");
    say("     starting Mesa's v3d driver through RISC3D%s\n", getenv("V3DM$Fake") ? " (V3DM$Fake: no GPU)" : "");
    c = v3dm_create(V3DM_API_GLES2, 2, 0, 0);
    if (!c) {
        say("FAIL no context: %s\n", v3dm_error());
        return 1;
    }
    say("ok   context: %s\n", v3dm_renderer());
    say("     GPU module: %s\n", v3dm_device());
    say("     making it current on a %dx%d frame (%s)\n", W, H, v3dm_formats(c));
    if (!v3dm_make_current(c, W, H)) {
        say("FAIL make current\n");
        return 1;
    }
    say("GL_VERSION  %s\n", (const char *) glGetString(GL_VERSION));
    say("GLSL        %s\n", (const char *) glGetString(GL_SHADING_LANGUAGE_VERSION));

    t0 = clock();
    setup();
    say("     shaders and texture in %d cs\n", (int) ((clock() - t0) * 100 / CLOCKS_PER_SEC));
    if (failures) {
        say("FAIL: %d failures\n", failures);
        return 1;
    }

    say("     drawing\n");
    t0 = clock();
    draw(W, H);
    glFinish();
    say("     first draw in %d cs\n", (int) ((clock() - t0) * 100 / CLOCKS_PER_SEC));
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, frame);
    /* left half: yellow x tint (1, 0.5, 1) = orange */
    check(near(px(frame, W, 20, 20), 0xFF8000, 2), "orange: the colour shader and its tint uniform",
          px(frame, W, 20, 20), 0xFF8000);
    /* right half: s,t 0..1 over 128x256 pixels; squares of 8 texels */
    check(px(frame, W, 136, 8) == 0xFFFFFF, "checker white (texture, 0,0)", px(frame, W, 136, 8), 0xFFFFFF);
    check(px(frame, W, 152, 8) == 0x000000, "checker black (texture, 1,0)", px(frame, W, 152, 8), 0x000000);
    check(px(frame, W, 152, 40) == 0xFFFFFF, "checker white (texture, 1,1)", px(frame, W, 152, 40), 0xFFFFFF);
    /* the blended square: half blue over orange */
    check(near(px(frame, W, 64, 192), 0x804080, 3), "blue blended over orange",
          px(frame, W, 64, 192), 0x804080);
    check(px(frame, W, 64, 100) != px(frame, W, 64, 192), "blend only inside its square",
          px(frame, W, 64, 100), 0xFF8000);
    save_sprite("Picture", frame);
    say("     picture saved as Picture (sprite)\n");

    say("     making it current on a %dx%d frame\n", BIG_W, BIG_H);
    if (!v3dm_make_current(c, BIG_W, BIG_H)) {
        say("FAIL make current %dx%d\n", BIG_W, BIG_H);
        return 1;
    }
    t0 = clock();
    for (i = 0; i < frames; i++) {
        draw(BIG_W, BIG_H);
        glFinish();
    }
    say("     %d frames of %dx%d (clear, 3 quads, a texture, blending, glFinish) in %d cs\n",
        frames, BIG_W, BIG_H, (int) ((clock() - t0) * 100 / CLOCKS_PER_SEC));
    glReadPixels(BIG_W * 3 / 4 + 4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, one);
    check(one[0] == 255 && one[1] == 255 && one[2] == 255, "full-HD frame: checker white",
          (unsigned) one[0] << 16 | one[1] << 8 | one[2], 0xFFFFFF);
    glReadPixels(20, 20, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, one);
    check(near((unsigned) one[0] << 16 | one[1] << 8 | one[2], 0xFF8000, 2), "full-HD frame: orange",
          (unsigned) one[0] << 16 | one[1] << 8 | one[2], 0xFF8000);
    check(glGetError() == GL_NO_ERROR, "no GL error", 0, 0);

    glDeleteBuffers(1, &vbo);
    glDeleteTextures(1, &tex);
    glDeleteProgram(prog_colour);
    glDeleteProgram(prog_tex);
    v3dm_destroy(c);
    v3dm_shutdown();
    say("%s: %d failures\n", failures ? "FAIL" : "ALL PASS", failures);
    if (out)
        fclose(out);
    return failures != 0;
}
