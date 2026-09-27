/*
 * render-rows.c - riscos-mesa rendering check: operations that read the
 * colour buffer (blending, colour masking, logic ops).
 *
 * Mesa reads the destination row in place when the buffer is RGBA8 in
 * swrast's byte order and 4-byte aligned, and unpacks it into a
 * temporary array otherwise (patches/mesa riscos-direct-rows). Each scene
 * is drawn into an aligned buffer and into one a byte off alignment, so
 * both ways are checked: the two hashes of a case must be the same, and
 * the same as expected/render-rows.txt (see run.sh).
 * Usage: render-rows
 * Part of riscos-mesa, MIT licence.
 */
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define W 97             /* odd sizes: rows don't start on a round number */
#define H 61

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

static void reset(void)
{
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_COLOR_LOGIC_OP);
    glDisable(GL_BLEND);
    glClearColor(0.2f, 0.4f, 0.6f, 0.8f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* Smooth-shaded triangles overlapping each other and the edges */
static void triangles(void)
{
    int i;
    for (i = 0; i < 6; i++) {
        glBegin(GL_TRIANGLES);
        glColor4f(1, 0.1f * i, 0, 0.3f);  glVertex2f(-10 + 7 * i, -5);
        glColor4f(0, 1, 0.2f * i, 0.7f);  glVertex2f(W + 5, 13 * i);
        glColor4f(0.5f, 0, 1, 0.5f);      glVertex2f(20 * i, H + 9);
        glEnd();
    }
}

static void scene(int n)
{
    reset();
    switch (n) {
    case 0:     /* alpha blending */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        triangles();
        break;
    case 1:     /* additive blending, one colour per rectangle */
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glColor4f(0.3f, 0.1f, 0.2f, 0.5f); glRectf(3, 3, W - 20, H - 9);
        glColor4f(0.1f, 0.5f, 0.1f, 0.5f); glRectf(15, 10, W - 2, H - 2);
        break;
    case 2:     /* colour mask */
        glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
        triangles();
        break;
    case 3:     /* logic op */
        glEnable(GL_COLOR_LOGIC_OP);
        glLogicOp(GL_XOR);
        triangles();
        break;
    case 4:     /* blending then masking */
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_TRUE);
        triangles();
        break;
    }
    glFinish();
}

static const char *const names[] = {
    "blend-alpha", "blend-add", "mask", "logic-xor", "blend-mask"
};

int main(void)
{
    unsigned char *aligned = malloc(W * H * 4);
    unsigned char *raw = malloc(W * H * 4 + 1);
    unsigned char *misaligned = raw + 1;
    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 0, 0, 0, NULL);
    int n;

    if (!ctx || !aligned || !raw) {
        fprintf(stderr, "setup failed\n");
        return 1;
    }
    for (n = 0; n < 5; n++) {
        unsigned a, m;
        OSMesaMakeCurrent(ctx, aligned, GL_UNSIGNED_BYTE, W, H);
        scene(n);
        a = hash(aligned);
        OSMesaMakeCurrent(ctx, misaligned, GL_UNSIGNED_BYTE, W, H);
        scene(n);
        m = hash(misaligned);
        printf("%s aligned %08x\n", names[n], a);
        printf("%s misaligned %08x\n", names[n], m);
    }
    OSMesaDestroyContext(ctx);
    return 0;
}
