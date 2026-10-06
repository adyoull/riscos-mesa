/*
 * glsl-temps.c - riscos-mesa check: GLSL programs that need more than 256
 * temporaries (patch riscos-temps).
 *
 * Mesa compacts a program's temporaries, but gives up when an array is
 * indexed at run time; Mesa 20.3.5 then had room for only 256 and read
 * zeros (and dropped writes) beyond that: a loop counter that never
 * changed ("Infinite loop detected in fragment program") and wrong
 * colours. This draws such a program (a long chain of values, an array
 * indexed by a varying, a loop) and checks the colours against the same
 * sums worked out here.
 * Prints one line per case; run.sh compares them with
 * expected/glsl-temps.txt.
 * Part of riscos-mesa, MIT licence.
 */
#define GL_GLEXT_PROTOTYPES
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define W 16
#define H 4
#define CHAIN 400

static float buf[W * H * 4];

static GLuint shader(GLenum t, const char *s)
{
   GLuint o = glCreateShader(t);
   GLint ok;
   glShaderSource(o, 1, &s, 0);
   glCompileShader(o);
   glGetShaderiv(o, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      char log[2000];
      glGetShaderInfoLog(o, sizeof log, 0, log);
      printf("compile failed: %s\n", log);
      exit(1);
   }
   return o;
}

int main(void)
{
   static char fs[CHAIN * 80 + 2000];
   OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 0, 0, 0, NULL);
   GLuint p;
   char *q = fs;
   int i, x, bad = 0;

   OSMesaMakeCurrent(ctx, buf, GL_FLOAT, W, H);
   q += sprintf(q, "#version 120\nvarying vec4 tc;\nvoid main() {\n"
                "  float a[4]; a[0] = 0.1; a[1] = 0.2; a[2] = 0.3; a[3] = 0.4;\n"
                "  int ix = int(clamp(tc.x * 4.0, 0.0, 3.0));\n"
                "  float t0 = tc.x;\n");
   for (i = 1; i <= CHAIN; i++)
      q += sprintf(q, "  float t%d = t%d * 0.999 + %d.0 * 0.0001;\n", i, i - 1, i % 7);
   q += sprintf(q, "  float s = 0.0;\n"
                "  for (int i = 0; i < 3; i++) { s += a[ix]; a[ix] *= 0.5; }\n"
                "  gl_FragColor = vec4(fract(t%d * 4.0), s, float(ix) * 0.25, 1.0);\n}\n",
                CHAIN);
   p = glCreateProgram();
   glAttachShader(p, shader(GL_VERTEX_SHADER,
      "#version 120\nvarying vec4 tc;\n"
      "void main() { tc = gl_MultiTexCoord0; gl_Position = ftransform(); }\n"));
   glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
   glLinkProgram(p);
   glUseProgram(p);

   glClear(GL_COLOR_BUFFER_BIT);
   glBegin(GL_QUADS);
   glTexCoord2f(0, 0); glVertex2f(-1, -1);
   glTexCoord2f(1, 0); glVertex2f(1, -1);
   glTexCoord2f(1, 1); glVertex2f(1, 1);
   glTexCoord2f(0, 1); glVertex2f(-1, 1);
   glEnd();
   glFinish();

   /* the same sums here, for the bottom row's pixels */
   for (x = 0; x < W; x++) {
      const float *px = buf + x * 4;
      const float tx = (x + 0.5f) / W;
      const int ix = (int) fminf(fmaxf(tx * 4.0f, 0.0f), 3.0f);
      const float a = 0.1f * (ix + 1);
      float t = tx, s, e0;
      for (i = 1; i <= CHAIN; i++)
         t = t * 0.999f + (float) (i % 7) * 0.0001f;
      s = a + a * 0.5f + a * 0.25f;
      e0 = t * 4.0f - floorf(t * 4.0f);
      if (fabsf(px[0] - e0) > 0.002f || fabsf(px[1] - s) > 0.0001f ||
          fabsf(px[2] - ix * 0.25f) > 0.0001f)
         bad++;
   }
   printf("%d temporaries' worth of values, array indexed at run time, a loop: %s\n",
          CHAIN, bad ? "FAIL" : "ok");
   if (bad)
      printf("  pixel 0: %g %g %g (%d of %d pixels wrong)\n", buf[0], buf[1], buf[2], bad, W);
   OSMesaDestroyContext(ctx);
   return 0;
}
