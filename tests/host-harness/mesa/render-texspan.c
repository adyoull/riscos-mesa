/*
 * render-texspan.c - riscos-mesa rendering check: the fast textured
 * spans (persp_textured_triangle), in particular the NEON bilinear RGBA8
 * span (patch riscos-tex-neon), against the C code.
 *
 * 240 cases of two triangles each with a GL_LINEAR RGBA8 texture:
 * textures of 4x4 to 256x32 texels, GL_REPEAT or GL_CLAMP_TO_EDGE on each
 * axis, GL_MODULATE or GL_REPLACE, orthographic and perspective views,
 * GL_FASTEST and GL_NICEST perspective hints, smooth vertex colours,
 * texture coordinates from small to large (and some too large for the
 * NEON code, which hands those to the C code), into a buffer 97 pixels
 * wide so spans end part way through a group of four.
 * One hash per case; run.sh compares them with expected/render-texspan.txt
 * and arm/run-arm.sh also runs it with MESA_NO_NEON and requires the same.
 * Part of riscos-mesa, MIT licence.
 */
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 97
#define H 61

static unsigned char buf[W * H * 4];
static unsigned rng = 12345;

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
   static const int sizes[][2] = { { 4, 4 }, { 16, 8 }, { 64, 64 }, { 256, 32 }, { 8, 128 } };
   static unsigned char img[256 * 256 * 4];
   OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 0, 0, 0, NULL);
   GLuint tex;
   int c, i;

   OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H);
   glGenTextures(1, &tex);
   glBindTexture(GL_TEXTURE_2D, tex);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glEnable(GL_TEXTURE_2D);
   glShadeModel(GL_SMOOTH);

   for (c = 0; c < 240; c++) {
      const int *sz = sizes[c % 5];
      const int persp = (c / 5) & 1;
      const int fastest = (c / 10) & 1;
      const int modulate = (c / 20) & 1;
      const int wraps = (c / 40) % 3;          /* 0 repeat, 1 clamp, 2 mixed */
      const float range = (c % 7 == 6) ? 2.0e6f : (c % 3 == 2) ? 40.0f : 2.5f;
      int v;

      for (i = 0; i < sz[0] * sz[1] * 4; i++)
         img[i] = (unsigned char) rnd();
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz[0], sz[1], 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, img);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                      wraps == 0 ? GL_REPEAT : GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                      wraps == 1 ? GL_CLAMP_TO_EDGE : GL_REPEAT);
      glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, modulate ? GL_MODULATE : GL_REPLACE);
      glHint(GL_PERSPECTIVE_CORRECTION_HINT, fastest ? GL_FASTEST : GL_NICEST);

      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      if (persp)
         glFrustum(-1, 1, -0.7, 0.7, 1, 10);
      else
         glOrtho(-1, 1, -1, 1, -1, 1);
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      if (persp) {
         glTranslatef(0, 0, -2.5f);
         const float ax = frand(-70, 70), ay = frand(-70, 70);
         glRotatef(ax, 1, 0, 0);
         glRotatef(ay, 0, 1, 0);
      }
      glClearColor(0.1f, 0.2f, 0.3f, 0.4f);
      glClear(GL_COLOR_BUFFER_BIT);
      glBegin(GL_TRIANGLES);
      for (v = 0; v < 6; v++) {
         /* one call each, in order: the order arguments are worked out
          * in differs between compilers */
         float r[9];
         int j;
         for (j = 0; j < 4; j++)
            r[j] = frand(0, 1);
         for (j = 4; j < 7; j++)
            r[j] = frand(-range, range);
         r[7] = frand(-1.1f, 1.1f);
         r[8] = frand(-1.1f, 1.1f);
         glColor4f(r[0], r[1], r[2], r[3]);
         glTexCoord2f(r[4] + r[5], r[6]);
         glVertex2f(r[7], r[8]);
      }
      glEnd();
      glFinish();
      if (getenv("TEXSPAN_DUMP") && atoi(getenv("TEXSPAN_DUMP")) == c) {
         FILE *f = fopen("texspan.raw", "wb");
         fwrite(buf, 1, sizeof buf, f);
         fclose(f);
      }
      /* coordinates of 2e6 texels times FIXED_SCALE pass 2^31, where
       * lroundf's answer differs between machines (long is 64 bits on
       * the host): "undefined", left out of the comparison with
       * expected/ but still compared between NEON and the C code */
      printf("%s%3d %dx%d %s %s %s %s range %g: %08x\n",
             range > 1e6f ? "undefined " : "", c, sz[0], sz[1],
             persp ? "persp" : "ortho", fastest ? "fastest" : "nicest",
             modulate ? "modulate" : "replace",
             wraps == 0 ? "repeat" : wraps == 1 ? "clamp" : "clamp-s",
             range, hash());
   }
   OSMesaDestroyContext(ctx);
   return 0;
}
