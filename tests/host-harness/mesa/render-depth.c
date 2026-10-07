/*
 * render-depth.c - riscos-mesa rendering check: depth testing, Z
 * interpolation and BGRA colour buffers, in particular their NEON
 * versions (patch riscos-depth-neon), against the C code.
 *
 * 448 cases: for each depth buffer (16, 24, 24 + 8 stencil and 32 bits)
 * and colour buffer order (OSMESA_RGBA and OSMESA_BGRA): a random
 * background and random depth buffer, then smooth triangles with every
 * depth function, depth writes on and off, some blended (which reads the
 * colour buffer back) and some textured, into a buffer 93 pixels wide so
 * spans end part way through a group of eight. One hash per case of the
 * colour buffer and of the depth buffer as glReadPixels gives it.
 * run.sh compares them with expected/render-depth.txt and arm/run-arm.sh
 * also runs it with MESA_NO_NEON and requires the same.
 * Part of riscos-mesa, MIT licence.
 */
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

#define W 93
#define H 57

static unsigned char buf[W * H * 4];
static unsigned char bg[W * H * 4];
static float zbg[W * H];
static GLuint zread[W * H];
static unsigned rng = 777;

static unsigned rnd(void)
{
   rng = rng * 1103515245u + 12345u;
   return rng >> 8;
}

static float frand(float lo, float hi)
{
   return lo + (hi - lo) * (float) (rnd() & 0xFFFF) / 65535.0f;
}

static unsigned hash(const unsigned char *p, int n)
{
   unsigned h = 2166136261u;
   int i;
   for (i = 0; i < n; i++) {
      h ^= p[i];
      h *= 16777619u;
   }
   return h;
}

int main(void)
{
   static const int depths[4][2] = { { 16, 0 }, { 24, 0 }, { 24, 8 }, { 32, 0 } };
   static const GLenum funcs[8] = { GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL,
                                    GL_EQUAL, GL_NOTEQUAL, GL_ALWAYS, GL_NEVER };
   static unsigned char img[64 * 64 * 4];
   int d, o, c, i, v;

   for (i = 0; i < 64 * 64 * 4; i++)
      img[i] = (unsigned char) rnd();
   for (d = 0; d < 4; d++)
      for (o = 0; o < 2; o++) {
         OSMesaContext ctx = OSMesaCreateContextExt(o ? OSMESA_BGRA : OSMESA_RGBA,
                                                    depths[d][0], depths[d][1], 0, NULL);
         GLuint tex;
         if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
            printf("no context %d/%d %s\n", depths[d][0], depths[d][1], o ? "bgra" : "rgba");
            continue;
         }
         glMatrixMode(GL_PROJECTION);
         glLoadIdentity();
         glOrtho(-1, 1, -1, 1, -1, 1);
         glMatrixMode(GL_MODELVIEW);
         glLoadIdentity();
         glGenTextures(1, &tex);
         glBindTexture(GL_TEXTURE_2D, tex);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
         glShadeModel(GL_SMOOTH);
         for (c = 0; c < 56; c++) {
            const GLenum func = funcs[c % 8];
            const int zwrite = (c / 8) % 2;
            const int blend = (c / 16) % 2;
            const int textured = c >= 32;
            for (i = 0; i < W * H * 4; i++)
               bg[i] = (unsigned char) rnd();
            for (i = 0; i < W * H; i++)
               zbg[i] = frand(0, 1);
            glDisable(GL_BLEND);
            glDisable(GL_TEXTURE_2D);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_ALWAYS);
            glDepthMask(GL_TRUE);
            glRasterPos2f(-1, -1);
            glDrawPixels(W, H, GL_RGBA, GL_UNSIGNED_BYTE, bg);
            glColorMask(0, 0, 0, 0);
            glDrawPixels(W, H, GL_DEPTH_COMPONENT, GL_FLOAT, zbg);
            glColorMask(1, 1, 1, 1);
            glDepthFunc(func);
            glDepthMask(zwrite ? GL_TRUE : GL_FALSE);
            if (blend) {
               glEnable(GL_BLEND);
               glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }
            if (textured)
               glEnable(GL_TEXTURE_2D);
            glBegin(GL_TRIANGLES);
            for (v = 0; v < 9; v++) {
               /* one call each, in order: the order arguments are worked
                * out in differs between compilers */
               float r[9];
               int j;
               for (j = 0; j < 9; j++)
                  r[j] = frand(0, 1);
               glColor4f(r[0], r[1], r[2], r[3]);
               glTexCoord2f(r[4] * 3, r[5] * 3);
               glVertex3f(r[6] * 2.4f - 1.2f, r[7] * 2.4f - 1.2f, r[8] * 1.8f - 0.9f);
            }
            glEnd();
            glFinish();
            glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, zread);
            printf("%2d/%d %s %3d func %04x write %d blend %d tex %d: %08x %08x\n",
                   depths[d][0], depths[d][1], o ? "bgra" : "rgba", c, func,
                   zwrite, blend, textured, hash(buf, sizeof buf),
                   hash((const unsigned char *) zread, sizeof zread));
         }
         glDeleteTextures(1, &tex);
         OSMesaDestroyContext(ctx);
      }
   return 0;
}
