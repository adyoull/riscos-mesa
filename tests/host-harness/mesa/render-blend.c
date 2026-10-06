/*
 * render-blend.c - riscos-mesa rendering check: alpha blending
 * (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA: swrast's blend_transparency_ubyte)
 * and smooth colour spans (interpolate_int_colors), in particular their
 * NEON versions (patch riscos-blend-neon), against the C code.
 *
 * 300 cases: a random background (glDrawPixels), then triangles with
 * smooth random colours and alpha (alpha 0 and 1 at some corners, so the
 * C code's special cases come up), some depth-tested against a random
 * depth buffer so that spans have masked-out pixels, some flat shaded,
 * some in one colour (smooth shading that doesn't change), some with
 * blending off (smooth spans only), into a buffer 93 pixels
 * wide so spans end part way through a group of eight.
 * One hash per case; run.sh compares them with expected/render-blend.txt
 * and arm/run-arm.sh also runs it with MESA_NO_NEON and requires the same.
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
static unsigned rng = 4321;

static unsigned rnd(void)
{
   rng = rng * 1103515245u + 12345u;
   return rng >> 8;
}

static float frand(float lo, float hi)
{
   return lo + (hi - lo) * (float) (rnd() & 0xFFFF) / 65535.0f;
}

static unsigned hash(void)
{
   unsigned h = 2166136261u;
   int i;
   for (i = 0; i < W * H * 4; i++) {
      h ^= buf[i];
      h *= 16777619u;
   }
   return h;
}

int main(void)
{
   OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 0, 0, NULL);
   int c, i, v;

   OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H);
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(-1, 1, -1, 1, -1, 1);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();

   for (c = 0; c < 300; c++) {
      const int depth = (c / 3) % 2;           /* masked pixels */
      const int flat = c % 5 == 4;
      const int blend = c % 7 != 6;            /* else smooth spans only */
      const int tris = 1 + c % 4;
      const int one = c % 11 == 10;            /* one colour, smooth */
      float col[4];

      for (i = 0; i < W * H * 4; i++)
         bg[i] = (unsigned char) rnd();
      for (i = 0; i < W * H; i++)
         zbg[i] = frand(0, 1);
      glDisable(GL_BLEND);
      glDisable(GL_DEPTH_TEST);
      glRasterPos2f(-1, -1);
      glDrawPixels(W, H, GL_RGBA, GL_UNSIGNED_BYTE, bg);
      if (depth) {
         glEnable(GL_DEPTH_TEST);
         glDepthFunc(GL_ALWAYS);
         glColorMask(0, 0, 0, 0);
         glDrawPixels(W, H, GL_DEPTH_COMPONENT, GL_FLOAT, zbg);
         glColorMask(1, 1, 1, 1);
         glDepthFunc(GL_LESS);
      }
      else
         glDisable(GL_DEPTH_TEST);
      glShadeModel(flat ? GL_FLAT : GL_SMOOTH);
      if (blend) {
         glEnable(GL_BLEND);
         glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      }
      for (i = 0; i < 4; i++)
         col[i] = frand(0, 1);
      glBegin(GL_TRIANGLES);
      for (v = 0; v < 3 * tris; v++) {
         /* one call each, in order: the order arguments are worked out
          * in differs between compilers */
         float r[7];
         int j;
         for (j = 0; j < 3; j++)
            r[j] = frand(0, 1);
         j = rnd() % 4;
         r[3] = j == 0 ? 0.0f : j == 1 ? 1.0f : frand(0, 1);
         r[4] = frand(-1.2f, 1.2f);
         r[5] = frand(-1.2f, 1.2f);
         r[6] = frand(-0.9f, 0.9f);
         if (one)
            glColor4fv(col);
         else
            glColor4f(r[0], r[1], r[2], r[3]);
         glVertex3f(r[4], r[5], r[6]);
      }
      glEnd();
      glFinish();
      printf("%3d %s %s %s %d: %08x\n", c, depth ? "depth" : "nodepth",
             flat ? "flat" : one ? "onecolour" : "smooth",
             blend ? "blend" : "noblend", tris,
             hash());
   }
   OSMesaDestroyContext(ctx);
   return 0;
}
