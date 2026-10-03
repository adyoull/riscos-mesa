/* render-matrix.c: immediate-mode drawing followed by a matrix push.
 *
 * glPushMatrix must flush the vertices still buffered from glBegin/glEnd
 * before it changes the matrix stack, or classic swrast draws them with
 * the pushed matrix. Warzone 2100's text code (QuesoGLC) does this on the
 * texture matrix: textured quads drawn with a texture-matrix scale came
 * out with the wrong texture coordinates, fell outside a GL_CLAMP texture
 * and were discarded by the alpha test. Found and reduced by the Warzone
 * 2100 port (its handoff of 2026-09-27; patch riscos-push-flush).
 *
 * Prints, for each variant, how many of three quads were drawn. All
 * should draw all three.
 */
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>

#define W 200
#define H 60
static unsigned char buf[W * H * 4], tex[64 * 64 * 4];

static void quad(float x)
{
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0, 0);   glVertex2f(x, 10);      glTexCoord2f(32, 0);  glVertex2f(x + 30, 10);
    glTexCoord2f(0, 32);  glVertex2f(x, 40);      glTexCoord2f(32, 32); glVertex2f(x + 30, 40);
    glEnd();
}

static int run(int variant)
{
    int x, shown = 0;
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glScalef(1 / 64.f, 1 / 64.f, 1);
    glMatrixMode(GL_MODELVIEW);
    glClearColor(0, 0, 0.5f, 1); glClear(GL_COLOR_BUFFER_BIT);
    glColor4ub(255, 255, 255, 255);
    quad(10); quad(60); quad(110);
    switch (variant) {
    case 0:     /* Warzone's sequence */
        glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glTranslatef(0, 0, 0);
        glMatrixMode(GL_TEXTURE); glPopMatrix();
        glMatrixMode(GL_MODELVIEW); glPopMatrix();
        break;
    case 1:     /* texture push and pop only */
        glMatrixMode(GL_TEXTURE); glPushMatrix(); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
        break;
    case 2:     /* modelview push, load, pop */
        glPushMatrix(); glLoadIdentity(); glPopMatrix();
        break;
    case 3:     /* glFlush first */
        glFlush();
        glMatrixMode(GL_TEXTURE); glPushMatrix(); glLoadIdentity(); glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        break;
    }
    glFinish();
    for (x = 25; x <= 125; x += 50) {
        const unsigned char *p = buf + ((H - 1 - 25) * W + x) * 4;   /* OSMesa: row 0 at the bottom */
        if (p[0] > 200)
            shown++;
    }
    return shown;
}

int main(void)
{
    int v;
    OSMesaContext c = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
    GLuint t;
    if (!c || !OSMesaMakeCurrent(c, buf, GL_UNSIGNED_BYTE, W, H))
        return 2;
    memset(tex, 255, sizeof tex);
    glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.1f);
    for (v = 0; v < 4; v++)
        printf("matrix push after glBegin/glEnd, variant %d: %d of 3 quads drawn\n", v, run(v));
    return 0;
}
