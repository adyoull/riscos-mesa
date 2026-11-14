/*
 * multitex.c - two texture units, selected with the ARB names as TORCS
 * and plib do (glActiveTextureARB, glClientActiveTextureARB,
 * glMultiTexCoord2fvARB), through freeglut, in immediate mode and with
 * client arrays.
 *
 * Unit 0 has a red texture (GL_MODULATE on white), unit 1 a green one
 * (GL_ADD): drawn right, the quad is yellow. If the unit selection is
 * lost, the green texture replaces the red one on unit 0 and the quad is
 * green. Prints the renderer and one line per check, "ok" or "FAIL";
 * writes them to the file named on the command line as well.
 *
 *   multitex [RESULTS-FILE]
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

static void check(int ok, const char *what, unsigned int a, unsigned int b)
{
    failures += !ok;
    say("%s %x %x\n", ok ? "ok  " : "FAIL", a, b);
    say("     %s\n", what, 0, 0);
}

static GLuint texture(const GLubyte rgb[3])
{
    GLuint t;
    GLubyte px[4 * 4 * 3];
    int i;
    for (i = 0; i < 16; i++)
        memcpy(px + i * 3, rgb, 3);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 4, 0, GL_RGB, GL_UNSIGNED_BYTE, px);
    return t;
}

static unsigned int centre(void)
{
    GLubyte p[4];
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return (unsigned int) p[0] << 16 | (unsigned int) p[1] << 8 | p[2];
}

static void units(GLuint red, GLuint green)
{
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, red);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, green);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
}

static void off(void)
{
    glActiveTextureARB(GL_TEXTURE1_ARB);
    glDisable(GL_TEXTURE_2D);
    glActiveTextureARB(GL_TEXTURE0_ARB);
    glDisable(GL_TEXTURE_2D);
}

int main(int argc, char **argv)
{
    static const GLubyte r[3] = { 255, 0, 0 }, g[3] = { 0, 255, 0 };
    static const GLfloat v[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
    static const GLfloat tc[] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    static const GLushort idx[] = { 0, 1, 2, 0, 2, 3 };
    GLfloat t2[2] = { 0.5f, 0.5f };
    GLuint red, green;
    GLint u = 0, units_max = 0;
    const char *s;

    glutInit(&argc, argv);
    out = argc > 1 ? fopen(argv[1], "w") : NULL;
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
    glutInitWindowSize(64, 64);
    glutCreateWindow("multitex");

    s = (const char *) glGetString(GL_RENDERER);
    say("GL_RENDERER %s\n", s ? s : "(none)", 0, 0);
    s = (const char *) glGetString(GL_VERSION);
    say("GL_VERSION  %s\n", s ? s : "(none)", 0, 0);
    s = (const char *) glGetString(GL_EXTENSIONS);
    glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &units_max);
    check(s && strstr(s, "GL_ARB_multitexture") && units_max >= 2,
          "GL_ARB_multitexture, GL_MAX_TEXTURE_UNITS (second number)", s && strstr(s, "GL_ARB_multitexture"), units_max);

    glActiveTextureARB(GL_TEXTURE1_ARB);
    glGetIntegerv(GL_ACTIVE_TEXTURE_ARB, &u);
    check(u == GL_TEXTURE1_ARB, "glActiveTextureARB(GL_TEXTURE1): GL_ACTIVE_TEXTURE", u, GL_TEXTURE1_ARB);
    glClientActiveTextureARB(GL_TEXTURE1_ARB);
    glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE_ARB, &u);
    check(u == GL_TEXTURE1_ARB, "glClientActiveTextureARB(GL_TEXTURE1): GL_CLIENT_ACTIVE_TEXTURE", u, GL_TEXTURE1_ARB);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);
    glActiveTextureARB(GL_TEXTURE0_ARB);

    red = texture(r);
    green = texture(g);
    glViewport(0, 0, 64, 64);
    glClearColor(0, 0, 1, 1);
    glColor4f(1, 1, 1, 1);

    /* immediate mode, glMultiTexCoord2fvARB */
    glClear(GL_COLOR_BUFFER_BIT);
    units(red, green);
    glBegin(GL_QUADS);
    glMultiTexCoord2fvARB(GL_TEXTURE0_ARB, t2);
    glMultiTexCoord2fvARB(GL_TEXTURE1_ARB, t2);
    glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
    glEnd();
    off();
    check(centre() == 0xFFFF00, "immediate mode, two units: yellow (red + green)", centre(), 0xFFFF00);

    /* client arrays, a texture-coordinate array per unit, glDrawElements */
    glClear(GL_COLOR_BUFFER_BIT);
    units(red, green);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, v);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glClientActiveTextureARB(GL_TEXTURE1_ARB);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, tc);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(GL_TEXTURE0_ARB);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    off();
    check(centre() == 0xFFFF00, "client arrays, two units: yellow (red + green)", centre(), 0xFFFF00);
    check(glGetError() == GL_NO_ERROR, "no GL error", 0, 0);

    say("%s: %x failures\n", failures ? "FAIL" : "ALL PASS", (unsigned int) failures, 0);
    if (out)
        fclose(out);
    return failures != 0;
}
