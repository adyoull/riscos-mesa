/*
 * render-stretch.c - riscos-mesa rendering check: textured quads drawn
 * square to the screen (a picture scaled up, as TORCS draws its scene
 * at half size and stretches it), whose spans keep to one texture row
 * pair: the NEON row gathers and the NEON bilinear sums of patch
 * riscos-zfirst, and that patch's depth test before texturing, against
 * the code before it.
 *
 * 240 cases: RGBA8 and RGB textures of 16x16 to 512x256, GL_REPEAT or
 * GL_CLAMP_TO_EDGE, GL_REPLACE or GL_MODULATE with a smooth colour,
 * GL_FASTEST or GL_NICEST, scales from 4 times larger to a little
 * smaller, texture coordinates starting anywhere (so groups of pixels
 * near the texture's edges and its wrap), some depth-tested over a
 * random depth buffer (half of them with the alpha test on), into a
 * buffer 157 pixels wide. One hash per case of the colour buffer.
 * run.sh compares them with expected/render-stretch.txt (made with the
 * code before the patch) and arm/run-arm.sh also runs it with
 * MESA_NO_NEON and requires the same.
 * Part of riscos-mesa, MIT licence.
 */
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>

#define W 157
#define H 61

static unsigned char buf[W * H * 4];
static float zbg[W * H];
static unsigned char img[512 * 256 * 4];
static unsigned rng = 31337;

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
   static const int sizes[5][2] = { { 16, 16 }, { 64, 32 }, { 128, 128 }, { 256, 64 }, { 512, 256 } };
   OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 0, 0, NULL);
   GLuint tex;
   int c, i;

   if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
      printf("no context\n");
      return 1;
   }
   for (i = 0; i < 512 * 256 * 4; i++)
      img[i] = (unsigned char) rnd();
   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glShadeModel(GL_SMOOTH);
   for (c = 0; c < 240; c++) {
      const int sz = c % 5, rgb = (c / 5) % 2, clamp = (c / 10) % 2;
      const int modulate = (c / 20) % 2, fastest = (c / 40) % 2;
      const int depth = (c / 80) % 3;            /* none, depth test, + alpha test */
      const int tw = sizes[sz][0], th = sizes[sz][1];
      const float scale = frand(0.8f, 4.0f);     /* screen pixels a texel */
      const float s0 = frand(-1.5f, 1.5f), t0 = frand(-1.5f, 1.5f);
      const float s1 = s0 + W / scale / tw, t1 = t0 + H / scale / th;
      for (i = 0; i < W * H * 4; i++)
         buf[i] = (unsigned char) rnd();
      glTexImage2D(GL_TEXTURE_2D, 0, rgb ? GL_RGB : GL_RGBA, tw, th, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, img);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, modulate ? GL_MODULATE : GL_REPLACE);
      glHint(GL_PERSPECTIVE_CORRECTION_HINT, fastest ? GL_FASTEST : GL_NICEST);
      glViewport(0, 0, W, H);
      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glOrtho(0, 1, 0, 1, -1, 1);
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      glDisable(GL_ALPHA_TEST);
      glDisable(GL_TEXTURE_2D);
      if (depth) {
         for (i = 0; i < W * H; i++)
            zbg[i] = frand(0, 1);
         glEnable(GL_DEPTH_TEST);
         glDepthFunc(GL_ALWAYS);
         glColorMask(0, 0, 0, 0);
         glRasterPos2f(0, 0);
         glDrawPixels(W, H, GL_DEPTH_COMPONENT, GL_FLOAT, zbg);
         glColorMask(1, 1, 1, 1);
         glDepthFunc(GL_LESS);
         if (depth == 2) {
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(GL_GREATER, 0.3f);
         }
      }
      else
         glDisable(GL_DEPTH_TEST);
      glEnable(GL_TEXTURE_2D);
      glBegin(GL_TRIANGLE_STRIP);
      for (i = 0; i < 4; i++) {
         /* one call each, in order: the order arguments are worked out
          * in differs between compilers */
         float r[5];
         int j;
         for (j = 0; j < 5; j++)
            r[j] = frand(0, 1);
         glColor4f(r[0], r[1], r[2], r[3]);
         glTexCoord2f(i & 1 ? s1 : s0, i & 2 ? t1 : t0);
         glVertex3f(i & 1, i >> 1, r[4] * 1.8f - 0.9f);
      }
      glEnd();
      glFinish();
      printf("%3d %3dx%-3d %s %s %s %s depth %d scale %.2f s0 %.2f t0 %.2f: %08x\n",
             c, tw, th, rgb ? "rgb " : "rgba", clamp ? "clamp " : "repeat",
             modulate ? "modulate" : "replace ", fastest ? "fastest" : "nicest ",
             depth, scale, s0, t0, hash(buf, sizeof buf));
   }
   OSMesaDestroyContext(ctx);
   return 0;
}
