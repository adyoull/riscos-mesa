/*
 * render-alphafast.c - riscos-mesa rendering check: the alpha test,
 * opaque blending and NEON fog on the fast route for depth-tested spans
 * (patch riscos-alpha-fast), against the code before it.
 *
 * 432 cases: for each depth and colour buffer (16-bit RGBA, 24-bit RGBA,
 * 24-bit BGRA): a random background and depth buffer, then perspective
 * triangles with the alpha test on (every function, references 0, a
 * little over 0, a half and 1), untextured or textured (an opaque RGBA8
 * texture, one with random alpha, a cut-out one, a nearly opaque one
 * (alpha 255 and some 254), an RGB one), vertex alpha 1 or random,
 * blended with GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA (or two other blend
 * functions) or not, with no fog, vertex fog and pixel fog, into a buffer
 * 93 pixels wide. One hash per case of the colour buffer and of the
 * depth buffer as glReadPixels gives it. run.sh compares them with
 * expected/render-alphafast.txt (made with the code before the patch)
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
static unsigned rng = 9191;

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
   static const int bufs[3][3] = { { 16, 0, 0 }, { 24, 0, 0 }, { 24, 0, 1 } };
   static const GLenum afuncs[8] = { GL_GREATER, GL_GEQUAL, GL_LESS, GL_LEQUAL,
                                     GL_EQUAL, GL_NOTEQUAL, GL_ALWAYS, GL_NEVER };
   static const GLfloat refs[4] = { 0.0f, 0.003f, 0.5f, 1.0f };
   static const GLfloat fogc[4] = { 0.3f, 0.5f, 0.7f, 1.0f };
   static unsigned char img[4][64 * 32 * 4];
   int b, c, i, v;

   for (i = 0; i < 64 * 32 * 4; i++) {
      img[0][i] = (i % 4 == 3) ? 255 : (unsigned char) rnd();  /* opaque */
      img[1][i] = (unsigned char) rnd();                        /* random alpha */
      img[2][i] = (i % 4 == 3) ? ((rnd() & 1) ? 255 : 0) : (unsigned char) rnd(); /* cut-out */
      img[3][i] = (i % 4 == 3) ? (rnd() % 40 ? 255 : 254) : (unsigned char) rnd(); /* nearly opaque */
   }
   for (b = 0; b < 3; b++) {
      OSMesaContext ctx = OSMesaCreateContextExt(bufs[b][2] ? OSMESA_BGRA : OSMESA_RGBA,
                                                 bufs[b][0], bufs[b][1], 0, NULL);
      GLuint tex[5];
      if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
         printf("no context %d/%d %s\n", bufs[b][0], bufs[b][1],
                bufs[b][2] ? "bgra" : "rgba");
         continue;
      }
      glGenTextures(5, tex);
      for (i = 0; i < 5; i++) {
         glBindTexture(GL_TEXTURE_2D, tex[i]);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
         glTexImage2D(GL_TEXTURE_2D, 0, i == 4 ? GL_RGB : GL_RGBA, 64, 32, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, img[i == 4 ? 0 : i]);
      }
      glShadeModel(GL_SMOOTH);
      glFogfv(GL_FOG_COLOR, fogc);
      for (c = 0; c < 144; c++) {
         /* every texture with every blend, fog and vertex alpha; a third
          * of the cases with TORCS's alpha test (GL_GREATER, 0) */
         const int texmode = c % 6;         /* none, opaque, random alpha, cut-out, nearly opaque, RGB */
         const int blend = (c / 6) % 4;     /* none, transparency, ONE/ONE_MINUS_SRC_ALPHA, SRC_ALPHA/ONE */
         const int fog = (c / 24) % 3;      /* none, vertex, pixel */
         const int valpha = (c / 72) % 2;   /* vertex alpha 1, or random */
         const GLenum afunc = c % 3 == 0 ? GL_GREATER : afuncs[(c / 3) % 8];
         const GLfloat ref = c % 3 == 0 ? 0.0f : refs[(c / 5) % 4];
         const int fastest = (c / 7) % 2;
         for (i = 0; i < W * H * 4; i++)
            bg[i] = (unsigned char) rnd();
         for (i = 0; i < W * H; i++)
            zbg[i] = frand(0.2f, 1);
         glDisable(GL_BLEND);
         glDisable(GL_TEXTURE_2D);
         glDisable(GL_FOG);
         glDisable(GL_ALPHA_TEST);
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
         glMatrixMode(GL_PROJECTION);
         glLoadIdentity();
         glFrustum(-0.5, 0.5, -0.3, 0.3, 1, 10);
         glMatrixMode(GL_MODELVIEW);
         glDepthFunc(rnd() % 2 ? GL_LESS : GL_LEQUAL);
         glDepthMask(rnd() % 4 ? GL_TRUE : GL_FALSE);
         glHint(GL_PERSPECTIVE_CORRECTION_HINT, fastest ? GL_FASTEST : GL_NICEST);
         glEnable(GL_ALPHA_TEST);
         glAlphaFunc(afunc, ref);
         if (texmode) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex[texmode - 1]);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
                      (c / 9) % 3 == 2 ? GL_REPLACE : GL_MODULATE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                            (c / 11) % 2 ? GL_REPEAT : GL_CLAMP_TO_EDGE);
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
            if (blend == 1)
               glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            else if (blend == 2)
               glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            else
               glBlendFunc(GL_SRC_ALPHA, GL_ONE);
         }
         glBegin(GL_TRIANGLES);
         for (v = 0; v < 12; v++) {
            /* one call each, in order: the order arguments are worked
             * out in differs between compilers */
            float r[9];
            int j;
            for (j = 0; j < 9; j++)
               r[j] = frand(0, 1);
            glColor4f(r[0], r[1], r[2], valpha ? r[3] : 1.0f);
            glTexCoord2f(r[4] * 6 - 3, r[5] * 6 - 3);
            glVertex3f(r[6] * 4 - 2, r[7] * 2.4f - 1.2f, -1.2f - r[8] * 7);
         }
         glEnd();
         glFinish();
         glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, zread);
         printf("%2d/%d %s %2d alpha %04x %.3f tex %d fog %d blend %d valpha %d fastest %d:"
                " %08x %08x\n",
                bufs[b][0], bufs[b][1], bufs[b][2] ? "bgra" : "rgba", c, afunc, ref,
                texmode, fog, blend, valpha, fastest, hash(buf, sizeof buf),
                hash((const unsigned char *) zread, sizeof zread));
      }
      glDeleteTextures(5, tex);
      OSMesaDestroyContext(ctx);
   }
   return 0;
}
