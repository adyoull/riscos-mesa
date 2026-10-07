/*
 * render-spanfast.c - riscos-mesa rendering check: the fused span write
 * for depth-tested spans (ro_write_rgba_span_fast) and the whole-span
 * NEON perspective textured spans (ro_neon_persp_span), patch
 * riscos-span-fast, against the general code.
 *
 * 192 cases: for each depth and colour buffer (16-bit RGBA, 24-bit RGBA,
 * 24-bit BGRA, 24-bit + 8 stencil RGBA): a random background and depth
 * buffer, then perspective triangles, untextured or textured (RGBA8 and
 * RGB textures, GL_MODULATE, GL_REPLACE and GL_DECAL), with GL_FASTEST
 * and GL_NICEST perspective hints, no fog, vertex fog and pixel fog,
 * blended or not, under a random depth function with depth writes on or
 * off, and some with a scissor box that cuts spans (which then take the
 * general code), into a buffer 93 pixels wide so spans end part way
 * through a group of four. One hash per case of the colour buffer and of
 * the depth buffer as glReadPixels gives it. run.sh compares them with
 * expected/render-spanfast.txt (made with the code before the patch)
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
static GLuint zread[W * H];
static unsigned rng = 4242;

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
   static const int bufs[4][3] = { { 16, 0, 0 }, { 24, 0, 0 }, { 24, 0, 1 }, { 24, 8, 0 } };
   static const GLenum funcs[4] = { GL_LESS, GL_LEQUAL, GL_GREATER, GL_ALWAYS };
   static const GLfloat fogc[4] = { 0.3f, 0.5f, 0.7f, 1.0f };
   static unsigned char img[64 * 32 * 4];
   int b, c, i, v;

   for (i = 0; i < 64 * 32 * 4; i++)
      img[i] = (unsigned char) rnd();
   for (b = 0; b < 4; b++) {
      OSMesaContext ctx = OSMesaCreateContextExt(bufs[b][2] ? OSMESA_BGRA : OSMESA_RGBA,
                                                 bufs[b][0], bufs[b][1], 0, NULL);
      GLuint tex[2];
      if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
         printf("no context %d/%d %s\n", bufs[b][0], bufs[b][1],
                bufs[b][2] ? "bgra" : "rgba");
         continue;
      }
      glGenTextures(2, tex);
      for (i = 0; i < 2; i++) {
         glBindTexture(GL_TEXTURE_2D, tex[i]);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexImage2D(GL_TEXTURE_2D, 0, i ? GL_RGB : GL_RGBA, 64, 32, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, img);
      }
      glShadeModel(GL_SMOOTH);
      glFogfv(GL_FOG_COLOR, fogc);
      for (c = 0; c < 48; c++) {
         const int texmode = c % 4;      /* none, RGBA8, RGB, RGB/RGBA8 replace */
         const int fastest = (c / 4) % 2;
         const int fog = (c / 8) % 3;    /* none, vertex, pixel */
         const int blend = (c / 24) % 2;
         const GLenum func = funcs[rnd() % 4];
         const int zwrite = rnd() % 2;
         const int scissor = c % 5 == 0;
         for (i = 0; i < W * H * 4; i++)
            bg[i] = (unsigned char) rnd();
         for (i = 0; i < W * H; i++)
            zbg[i] = frand(0.2f, 1);
         /* the background, in an orthographic view */
         glDisable(GL_BLEND);
         glDisable(GL_TEXTURE_2D);
         glDisable(GL_FOG);
         glDisable(GL_SCISSOR_TEST);
         glMatrixMode(GL_PROJECTION);
         glLoadIdentity();
         glOrtho(-1, 1, -1, 1, -1, 1);
         glMatrixMode(GL_MODELVIEW);
         glLoadIdentity();
         glEnable(GL_DEPTH_TEST);
         glDepthFunc(GL_ALWAYS);
         glDepthMask(GL_TRUE);
         glRasterPos2f(-1, -1);
         glDrawPixels(W, H, GL_RGBA, GL_UNSIGNED_BYTE, bg);
         glColorMask(0, 0, 0, 0);
         glDrawPixels(W, H, GL_DEPTH_COMPONENT, GL_FLOAT, zbg);
         glColorMask(1, 1, 1, 1);
         /* the triangles, in a perspective one */
         glMatrixMode(GL_PROJECTION);
         glLoadIdentity();
         glFrustum(-0.5, 0.5, -0.3, 0.3, 1, 10);
         glMatrixMode(GL_MODELVIEW);
         glDepthFunc(func);
         glDepthMask(zwrite ? GL_TRUE : GL_FALSE);
         glHint(GL_PERSPECTIVE_CORRECTION_HINT, fastest ? GL_FASTEST : GL_NICEST);
         if (texmode) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex[texmode == 1 ? 0 :
                                             texmode == 2 ? 1 : (c / 4) % 2]);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                      texmode < 3 ? GL_MODULATE : (c / 8) % 2 ? GL_DECAL : GL_REPLACE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                            (c / 2) % 3 ? GL_REPEAT : GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            (c / 3) % 2 ? GL_REPEAT : GL_CLAMP_TO_EDGE);
         }
         if (fog) {
            glEnable(GL_FOG);
            glFogi(GL_FOG_MODE, fog == 1 ? GL_LINEAR : GL_EXP);
            glFogf(GL_FOG_START, 1.5f);
            glFogf(GL_FOG_END, 12.0f);
            glFogf(GL_FOG_DENSITY, 0.15f);
            glHint(GL_FOG_HINT, fog == 1 ? GL_FASTEST : GL_NICEST);
         }
         if (blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
         }
         if (scissor) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(7 + c % 11, 5, W - 30, H - 12);
         }
         glBegin(GL_TRIANGLES);
         for (v = 0; v < 12; v++) {
            /* one call each, in order: the order arguments are worked
             * out in differs between compilers */
            float r[9];
            int j;
            for (j = 0; j < 9; j++)
               r[j] = frand(0, 1);
            glColor4f(r[0], r[1], r[2], r[3]);
            glTexCoord2f(r[4] * 6 - 3, r[5] * 6 - 3);
            glVertex3f(r[6] * 4 - 2, r[7] * 2.4f - 1.2f, -1.2f - r[8] * 7);
         }
         glEnd();
         glFinish();
         glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, zread);
         printf("%2d/%d %s %2d tex %d fastest %d fog %d blend %d func %04x write %d"
                " scissor %d: %08x %08x\n",
                bufs[b][0], bufs[b][1], bufs[b][2] ? "bgra" : "rgba", c, texmode,
                fastest, fog, blend, func, zwrite, scissor, hash(buf, sizeof buf),
                hash((const unsigned char *) zread, sizeof zread));
      }
      glDeleteTextures(2, tex);
      OSMesaDestroyContext(ctx);
   }
   return 0;
}
