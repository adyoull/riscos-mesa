/*
 * render-image.c - riscos-mesa rendering check: textures that use memory
 * they don't own (GL_OES_EGL_image, patches/mesa riscos-eglimage).
 *
 * An EGL library registers an image lookup with OSMesaSetImageLookup;
 * glEGLImageTargetTexture2DOES then makes a texture read the image's
 * pixels in place. This checker plays the EGL library: its "images" are
 * 32-bit pixel arrays in either byte order (R,G,B,x or B,G,R,x). Each case
 * draws a scene textured from an image, then the same scene with the same
 * pixels uploaded by glTexImage2D, and prints both hashes: they must be
 * equal, and equal to expected/render-image.txt (see run.sh). It also
 * checks that a change to the pixels shows at the next draw with no GL call
 * in between, and that re-specifying the texture leaves the pixels alone.
 * Usage: render-image
 * Part of riscos-mesa, MIT licence.
 */
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define W 80             /* the picture */
#define H 60
#define TW 37            /* the image: not powers of two */
#define TH 23

typedef void (*TargetTexture_t)(GLenum target, void *image);

struct image {
    unsigned char *pixels;
    int bgr;             /* byte order B,G,R,x (else R,G,B,x) */
};

static int lookups;

static GLboolean lookup(void *handle, OSMesaImage *desc)
{
    struct image *im = handle;
    lookups++;
    if (!im || !im->pixels)
        return GL_FALSE;
    desc->pixels = im->pixels;
    desc->width = TW;
    desc->height = TH;
    desc->row_bytes = TW * 4;
    desc->format = im->bgr ? OSMESA_BGRA : OSMESA_RGBA;
    return GL_TRUE;
}

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

/* Pattern k in the image's byte order; the fourth byte is junk (it must
   not be used: such textures are opaque). */
static void fill(struct image *im, int k)
{
    int x, y;
    for (y = 0; y < TH; y++)
        for (x = 0; x < TW; x++) {
            unsigned char *p = im->pixels + (y * TW + x) * 4;
            unsigned char r = x * 7 + k, g = y * 11 + k, b = (x ^ y) * 5 + k;
            p[0] = im->bgr ? b : r;
            p[1] = g;
            p[2] = im->bgr ? r : b;
            p[3] = (x * 31 + y) & 255;
        }
}

/* The same pixels as R,G,B for glTexImage2D */
static void as_rgb(const struct image *im, unsigned char *rgb)
{
    int i;
    for (i = 0; i < TW * TH; i++) {
        const unsigned char *p = im->pixels + i * 4;
        rgb[i * 3] = im->bgr ? p[2] : p[0];
        rgb[i * 3 + 1] = p[1];
        rgb[i * 3 + 2] = im->bgr ? p[0] : p[2];
    }
}

static void scene(int kind, GLenum filter)
{
    glClearColor(0.1f, 0.2f, 0.3f, 0.4f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glEnable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (kind == 0) {                    /* flat, replace */
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    } else if (kind == 1) {             /* tilted in perspective, modulate */
        glMatrixMode(GL_PROJECTION);
        glFrustum(-1, 1, -1, 1, 1, 10);
        glMatrixMode(GL_MODELVIEW);
        glTranslatef(0, 0, -2.2f);
        glRotatef(55, 1, 0.3f, 0);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(0.9f, 0.6f, 1.0f, 0.5f);
    } else {                            /* blended over the background */
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(1, 1, 1, 0.5f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glBegin(GL_QUADS);
    glTexCoord2f(0, 1); glVertex2f(-1, -1);
    glTexCoord2f(1, 1); glVertex2f(1, -1);
    glTexCoord2f(1, 0); glVertex2f(1, 1);
    glTexCoord2f(0, 0); glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_BLEND);
    glColor4f(1, 1, 1, 1);
    glFinish();
}

int main(void)
{
    static const char *kinds[] = { "flat-replace", "tilted-modulate", "blended" };
    static unsigned char buf[W * H * 4], rgb[TW * TH * 3];
    struct image im[2];
    TargetTexture_t target;
    OSMesaContext ctx;
    GLuint tex[2];
    int o, kind, f, failures = 0;
    unsigned a, b;

    OSMesaSetImageLookup(lookup);
    ctx = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
    if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H))
        return 1;
    printf("extension %s\n", strstr((const char *) glGetString(GL_EXTENSIONS),
                                    "GL_OES_EGL_image") ? "yes" : "no");
    target = (TargetTexture_t) OSMesaGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!target)
        return 1;
    glGenTextures(2, tex);

    for (o = 0; o < 2; o++) {
        im[o].bgr = o;
        im[o].pixels = malloc(TW * TH * 4);
        fill(&im[o], 0);
        for (kind = 0; kind < 3; kind++)
            for (f = 0; f < 2; f++) {
                GLenum filter = f ? GL_LINEAR : GL_NEAREST;
                glBindTexture(GL_TEXTURE_2D, tex[0]);
                target(GL_TEXTURE_2D, &im[o]);
                scene(kind, filter);
                a = hash(buf);
                glBindTexture(GL_TEXTURE_2D, tex[1]);
                as_rgb(&im[o], rgb);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TW, TH, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
                scene(kind, filter);
                b = hash(buf);
                printf("image %s %s %s %08x upload %08x %s\n", o ? "bgrx" : "rgbx", kinds[kind],
                       f ? "linear" : "nearest", a, b, a == b ? "same" : "DIFFERENT");
                failures += a != b;
            }
    }

    /* A new frame in the pixels shows at the next draw */
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    target(GL_TEXTURE_2D, &im[0]);
    fill(&im[0], 90);
    scene(0, GL_NEAREST);
    a = hash(buf);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    as_rgb(&im[0], rgb);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TW, TH, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    scene(0, GL_NEAREST);
    b = hash(buf);
    printf("live update %s\n", a == b ? "yes" : "NO");
    failures += a != b;

    /* Re-specifying the texture leaves the pixels alone (and doesn't free them) */
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, TW, TH, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    memset(im[0].pixels, 0x5a, TW * TH * 4);
    printf("respecified %s\n", glGetError() == GL_NO_ERROR ? "ok" : "ERROR");

    /* Errors */
    {
        struct image none = { NULL, 0 };
        target(GL_TEXTURE_2D, &none);
        printf("bad image %s\n", glGetError() == GL_INVALID_VALUE ? "GL_INVALID_VALUE" : "WRONG");
        target(GL_TEXTURE_2D, NULL);
        printf("null image %s\n", glGetError() == GL_INVALID_VALUE ? "GL_INVALID_VALUE" : "WRONG");
    }
    printf("lookups %d\n", lookups);

    glDeleteTextures(2, tex);
    OSMesaDestroyContext(ctx);
    free(im[0].pixels);
    free(im[1].pixels);
    return failures != 0;
}
