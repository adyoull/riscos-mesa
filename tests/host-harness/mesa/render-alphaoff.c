/*
 * render-alphaoff.c - riscos-mesa rendering check: an alpha test that
 * passes every fragment changes nothing. The same scene is drawn with
 * GL_ALPHA_TEST GL_GREATER 0.5 (a voxel game's whole terrain pass, as
 * ClassiCube draws it) and with no alpha test, and the two colour and
 * depth buffers must be the same, byte for byte. With an opaque texture
 * and vertex alpha 1 every fragment passes, so the spans with an alpha
 * test (the alpha-test route, patch riscos-alpha-fast) and those without
 * (the fused route, patch riscos-span-fast) must write the same pixels.
 *
 * The scene is a wall of small textured quads seen in perspective, spans
 * of a few pixels as in a voxel game, depth-tested over random depths,
 * with per-vertex shading. Cases: 16- and 24-bit depth, RGBA and BGRA,
 * an opaque RGBA8 texture filtered NEAREST or LINEAR, an RGB one, no fog,
 * vertex fog and pixel fog, GL_NICEST and GL_FASTEST hints. Each case
 * prints "same", or the two hashes when they differ; run.sh compares the
 * output with expected/render-alphaoff.txt (every case "same") and
 * arm/run-arm.sh runs it on the RISC OS build, with and without NEON.
 * Part of riscos-mesa, MIT licence.
 */
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>

#define W 97
#define H 61

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

/* the background (colour and depth), the same for both draws of a case */
static void clear_to_background(void)
{
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
}

/* a wall of 16 x 10 quads, each a tile of the texture, leaning away */
static void draw_wall(unsigned seed)
{
   int i, j;
   unsigned keep = rng;
   rng = seed;                          /* the same colours both times */
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glFrustum(-0.5, 0.5, -0.3, 0.3, 1, 20);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTranslatef(0.0f, 0.0f, -7.0f);
   glRotatef(40.0f, 0, 1, 0);
   glRotatef(-10.0f, 1, 0, 0);
   glTranslatef(-3.36f, -1.65f, 0.0f);      /* the wall's middle at the origin */
   glBegin(GL_QUADS);
   for (j = 0; j < 10; j++)
      for (i = 0; i < 16; i++) {
         const float s0 = (i % 4) * 0.25f, t0 = (j % 4) * 0.25f;
         const float x = i * 0.42f, y = j * 0.33f;
         const float z = frand(-0.15f, 0.15f);
         glColor4f(frand(0.5f, 1), frand(0.5f, 1), frand(0.5f, 1), 1.0f);
         glTexCoord2f(s0, t0);                 glVertex3f(x, y, z);
         glTexCoord2f(s0 + 0.25f, t0);         glVertex3f(x + 0.42f, y, z);
         glTexCoord2f(s0 + 0.25f, t0 + 0.25f); glVertex3f(x + 0.42f, y + 0.33f, z);
         glTexCoord2f(s0, t0 + 0.25f);         glVertex3f(x, y + 0.33f, z);
      }
   glEnd();
   glFinish();
   rng = keep;
}

int main(void)
{
   static const int bufs[4][3] = { { 16, 0, 0 }, { 24, 0, 0 }, { 16, 0, 1 }, { 24, 0, 1 } };
   static const GLfloat fogc[4] = { 0.6f, 0.7f, 0.9f, 1.0f };
   static unsigned char img[64 * 64 * 4];
   int b, c, i;

   for (i = 0; i < 64 * 64 * 4; i++)
      img[i] = (i % 4 == 3) ? 255 : (unsigned char) rnd();     /* opaque */
   for (b = 0; b < 4; b++) {
      OSMesaContext ctx = OSMesaCreateContextExt(bufs[b][2] ? OSMESA_BGRA : OSMESA_RGBA,
                                                 bufs[b][0], bufs[b][1], 0, NULL);
      GLuint tex[3];
      if (!ctx || !OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
         printf("no context %d %s\n", bufs[b][0], bufs[b][2] ? "bgra" : "rgba");
         continue;
      }
      glGenTextures(3, tex);
      for (i = 0; i < 3; i++) {
         const GLint filter = i == 1 ? GL_LINEAR : GL_NEAREST;
         glBindTexture(GL_TEXTURE_2D, tex[i]);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
         glTexImage2D(GL_TEXTURE_2D, 0, i == 2 ? GL_RGB : GL_RGBA, 64, 64, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, img);
      }
      glShadeModel(GL_SMOOTH);
      glFogfv(GL_FOG_COLOR, fogc);
      for (c = 0; c < 18; c++) {
         const int texi = c % 3;           /* RGBA nearest, RGBA linear, RGB nearest */
         const int fog = (c / 3) % 3;      /* none, vertex (FASTEST), pixel (NICEST) */
         const int fastest = (c / 9) % 2;
         const unsigned seed = rnd();
         unsigned h[2][2];
         int pass, drawn = 0;
         for (i = 0; i < W * H * 4; i++)
            bg[i] = (unsigned char) rnd();
         for (i = 0; i < W * H; i++)
            zbg[i] = frand(0.85f, 1);   /* the wall is at 0.87-0.93: some of it hidden */
         for (pass = 0; pass < 2; pass++) {
            clear_to_background();
            glDepthFunc(GL_LEQUAL);
            glHint(GL_PERSPECTIVE_CORRECTION_HINT, fastest ? GL_FASTEST : GL_NICEST);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex[texi]);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            if (fog) {
               glEnable(GL_FOG);
               glFogi(GL_FOG_MODE, GL_LINEAR);
               glFogf(GL_FOG_START, 4.0f);
               glFogf(GL_FOG_END, 14.0f);
               glHint(GL_FOG_HINT, fog == 1 ? GL_FASTEST : GL_NICEST);
            }
            if (pass == 0) {
               glEnable(GL_ALPHA_TEST);
               glAlphaFunc(GL_GREATER, 0.5f);
            }
            draw_wall(seed);
            if (pass == 1) {        /* the wall is really there: most pixels changed */
               unsigned char now[W * H * 4];
               glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, now);
               for (i = 0; i < W * H; i++)
                  drawn += memcmp(now + 4 * i, bg + 4 * i, 3) != 0;
            }
            glReadPixels(0, 0, W, H, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, zread);
            h[pass][0] = hash(buf, sizeof buf);
            h[pass][1] = hash((const unsigned char *) zread, sizeof zread);
         }
         printf("%2d %s %2d tex %d fog %d fastest %d: ", bufs[b][0], bufs[b][2] ? "bgra" : "rgba",
                c, texi, fog, fastest);
         if (drawn < W * H / 8)
            printf("only %d pixels drawn\n", drawn);
         else if (h[0][0] == h[1][0] && h[0][1] == h[1][1])
            printf("same\n");
         else
            printf("DIFFERENT: alpha test %08x %08x, none %08x %08x\n",
                   h[0][0], h[0][1], h[1][0], h[1][1]);
      }
      glDeleteTextures(3, tex);
      OSMesaDestroyContext(ctx);
   }
   return 0;
}
