/*
 * glsl-jit.c - riscos-mesa check: the shader JIT (patch riscos-glsl-jit)
 * draws what the interpreter draws, to within what nobody would see.
 *
 * Each shader is drawn twice into an RGBA8 buffer, with the JIT off and
 * on (_mesa_ro_jit_state), and the two pictures compared.
 *   - Typical shaders (lighting, fog, gamma, procedural patterns,
 *     glbench's): no channel may differ by more than 2 (of 255) except
 *     where the shader itself has a hard edge (step, floor, a cut-off),
 *     and there at most a few pixels.
 *   - Random shaders (jit-shaders.txt, from tools/gen-jit-shaders.py):
 *     mixes of everything the JIT compiles and what it doesn't, so its
 *     blocks start and end everywhere and run with lanes switched off.
 *     Chaotic expressions (fract of large numbers, steps on noise) turn
 *     float rounding into big differences in odd pixels, as they would
 *     between two GPUs, so this counts pixels: fewer than 1 in 200 may
 *     differ by more than 2.
 * Shaders that don't draw the same twice with the interpreter (they read
 * array elements they never wrote, left from earlier fragments) are
 * left out.
 * Half the draws use a polygon stipple, so the fragments the program runs
 * for aren't next to each other (the JIT gathers its inputs then).
 *
 *   glsl-jit [jit-shaders.txt [count]]
 * The last line says PASS or FAIL (the exit status is 0 either way, so
 * the harness doesn't take a FAIL for qemu failing). With a library without the JIT (the
 * host build) it says so and passes.
 * Part of riscos-mesa, MIT licence.
 */
#define GL_GLEXT_PROTOTYPES
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int _mesa_ro_jit_state __attribute__((weak));

#define W 64
#define H 48

static unsigned char bufA[W * H * 4], bufB[W * H * 4], *buf;
static OSMesaContext ctx;

static const char *VS =
    "#version 120\nvarying vec4 c; varying vec4 tc; varying vec3 n;\n"
    "void main(){ c = gl_Color; tc = gl_MultiTexCoord0; n = gl_Normal;"
    " gl_Position = ftransform(); }\n";

/* typical shaders; 'edge' ones have hard edges of their own */
static const struct { const char *name; int edge; const char *fs; } TYPICAL[] = {
   { "glbench", 0,
     "#version 120\nvarying vec3 n; varying vec4 c; uniform float t;\n"
     "void main() { float d = max(dot(normalize(n), normalize(vec3(0.4,0.6,0.8))), 0.0);\n"
     "  float s = 0.5 + 0.5 * sin(gl_FragCoord.y * 0.1 + t);\n"
     "  gl_FragColor = vec4(c.rgb * (0.25 + 0.75 * d) * (0.7 + 0.3 * s), 1.0); }\n" },
   { "phong", 0,
     "#version 120\nvarying vec3 n; varying vec4 c; varying vec4 tc;\n"
     "void main() { vec3 N = normalize(n); vec3 L = normalize(vec3(0.5, 0.7, 1.0));\n"
     "  vec3 V = normalize(vec3(tc.xy - 0.5, 1.0)); vec3 R = reflect(-L, N);\n"
     "  float sp = pow(max(dot(R, V), 0.0), 32.0); float df = max(dot(N, L), 0.0);\n"
     "  gl_FragColor = vec4(c.rgb * (0.2 + 0.8 * df) + vec3(sp), 1.0); }\n" },
   { "specular64", 0,
     "#version 120\nvarying vec3 n; varying vec4 tc;\n"
     "void main() { vec3 H = normalize(vec3(0.3, 0.4, 1.0) + vec3(tc.xy, 0.0));\n"
     "  float s = pow(max(dot(normalize(n), H), 0.0), 64.0);\n"
     "  gl_FragColor = vec4(s, s * 0.8, s * 0.6, 1.0); }\n" },
   { "fog", 0,
     "#version 120\nvarying vec4 c; varying vec4 tc;\n"
     "void main() { float z = gl_FragCoord.z / gl_FragCoord.w + tc.x * 4.0;\n"
     "  float f = exp(-0.3 * z * z); f = clamp(f, 0.0, 1.0);\n"
     "  gl_FragColor = vec4(mix(vec3(0.7, 0.8, 0.9), c.rgb, f), 1.0); }\n" },
   { "gamma", 0,
     "#version 120\nvarying vec4 c;\n"
     "void main() { gl_FragColor = vec4(pow(c.rgb, vec3(1.0 / 2.2)), c.a); }\n" },
   { "tonemap", 0,
     "#version 120\nvarying vec4 c; varying vec4 tc;\n"
     "void main() { vec3 x = c.rgb * (1.0 + 4.0 * tc.y);\n"
     "  vec3 m = x / (1.0 + x); gl_FragColor = vec4(exp2(log2(m + 0.001) * 0.8), 1.0); }\n" },
   { "plasma", 0,
     "#version 120\nvarying vec4 tc; uniform float t;\n"
     "void main() { float v = sin(tc.x * 10.0 + t) + sin(tc.y * 8.0 - t)\n"
     "    + sin((tc.x + tc.y) * 5.0) + cos(length(tc.xy - 0.5) * 12.0);\n"
     "  gl_FragColor = vec4(0.5 + 0.5 * sin(v), 0.5 + 0.5 * cos(v * 1.3), 0.5 + 0.5 * sin(v + 2.0), 1.0); }\n" },
   { "hsv", 0,
     "#version 120\nvarying vec4 tc;\n"
     "void main() { vec3 h = vec3(tc.x, 0.8, 0.9);\n"
     "  vec3 p = abs(fract(h.xxx + vec3(1.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0);\n"
     "  gl_FragColor = vec4(h.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), h.y), 1.0); }\n" },
   { "vignette", 0,
     "#version 120\nvarying vec4 c; varying vec4 tc;\n"
     "void main() { vec2 p = tc.xy - 0.5; float v = 1.0 - dot(p, p) * 1.5;\n"
     "  gl_FragColor = vec4(c.rgb * v, 1.0); }\n" },
   { "rim", 0,
     "#version 120\nvarying vec3 n; varying vec4 c;\n"
     "void main() { float r = 1.0 - max(dot(normalize(n), vec3(0.0, 0.0, 1.0)), 0.0);\n"
     "  gl_FragColor = vec4(c.rgb * 0.5 + vec3(r * r * r), 1.0); }\n" },
   { "spot", 0,
     "#version 120\nvarying vec4 tc; varying vec4 c;\n"
     "void main() { float d = distance(tc.xy, vec2(0.4, 0.6));\n"
     "  float s = 1.0 - smoothstep(0.1, 0.5, d); gl_FragColor = vec4(c.rgb * s, 1.0); }\n" },
   { "swirl", 0,
     "#version 120\nvarying vec4 tc; uniform float t;\n"
     "void main() { vec2 p = tc.xy - 0.5; float a = atan(p.y, p.x) + length(p) * 6.0 + t;\n"
     "  gl_FragColor = vec4(0.5 + 0.5 * cos(a), 0.5 + 0.5 * sin(a * 2.0), length(p), 1.0); }\n" },
   { "water", 0,
     "#version 120\nvarying vec4 tc; uniform float t;\n"
     "void main() { float w = sin(tc.x * 20.0 + t) * cos(tc.y * 15.0 - t * 0.7) * 0.5 + 0.5;\n"
     "  gl_FragColor = vec4(0.1, 0.3 + 0.3 * w, 0.6 + 0.4 * w, 1.0); }\n" },
   { "bloom", 0,
     "#version 120\nvarying vec4 c;\n"
     "void main() { vec3 b = max(c.rgb - 0.6, 0.0) * 2.5;\n"
     "  gl_FragColor = vec4(min(c.rgb + b, 1.0), 1.0); }\n" },
   { "invsqrt", 0,
     "#version 120\nvarying vec3 n; varying vec4 tc;\n"
     "void main() { vec3 v = n * (0.5 + tc.x); vec3 u = v * inversesqrt(dot(v, v));\n"
     "  gl_FragColor = vec4(u * 0.5 + 0.5, 1.0); }\n" },
   { "texlit", 0,
     "#version 120\nvarying vec3 n; varying vec4 tc; uniform sampler2D smp;\n"
     "void main() { vec4 tx = texture2D(smp, tc.xy * 2.0);\n"
     "  float d = max(dot(normalize(n), normalize(vec3(-0.3, 0.5, 1.0))), 0.0);\n"
     "  gl_FragColor = vec4(tx.rgb * (0.3 + 0.7 * d), tx.a); }\n" },
   { "branchy", 0,
     "#version 120\nvarying vec4 c; varying vec4 tc; varying vec3 n;\n"
     "void main() { vec3 col = c.rgb; float d = dot(normalize(n), vec3(0.0, 0.6, 0.8));\n"
     "  if (d > 0.3) { col = col * d + vec3(0.1, 0.05, 0.0); } else { col = col * 0.3 + vec3(sin(tc.x * 9.0) * 0.1); }\n"
     "  gl_FragColor = vec4(sqrt(max(col, 0.0)), 1.0); }\n" },
   { "toon", 1,
     "#version 120\nvarying vec3 n; varying vec4 c;\n"
     "void main() { float d = max(dot(normalize(n), normalize(vec3(0.3, 0.5, 1.0))), 0.0);\n"
     "  d = floor(d * 4.0) / 4.0; gl_FragColor = vec4(c.rgb * (0.3 + 0.7 * d), 1.0); }\n" },
   { "checker", 1,
     "#version 120\nvarying vec4 tc;\n"
     "void main() { float k = mod(floor(tc.x * 8.0) + floor(tc.y * 8.0), 2.0);\n"
     "  gl_FragColor = vec4(vec3(0.2 + 0.6 * k), 1.0); }\n" },
   { "circle", 1,
     "#version 120\nvarying vec4 tc;\n"
     "void main() { float r = length(tc.xy - 0.5); float e = step(r, 0.35);\n"
     "  gl_FragColor = vec4(e, 0.5 * e, 1.0 - e, 1.0); }\n" },
   { "cutout", 1,
     "#version 120\nvarying vec4 tc; varying vec4 c;\n"
     "void main() { if (fract(tc.x * 5.0) < 0.3) discard;\n"
     "  gl_FragColor = vec4(c.rgb * exp2(-tc.y), 1.0); }\n" },
};

static GLuint vs;

static GLuint shader(GLenum t, const char *s)
{
   GLuint o = glCreateShader(t);
   GLint ok;
   glShaderSource(o, 1, &s, 0);
   glCompileShader(o);
   glGetShaderiv(o, GL_COMPILE_STATUS, &ok);
   if (!ok) {
      glDeleteShader(o);
      return 0;
   }
   return o;
}

static const GLubyte stipple[128] = {
   0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0xF0, 0xF0, 0x0F, 0x0F,
   0xCC, 0x33, 0xCC, 0x33, 0x77, 0xEE, 0xBB, 0xDD, 0x5A, 0xA5, 0x3C, 0xC3,
   0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0xF0, 0xF0, 0x0F, 0x0F,
   0xCC, 0x33, 0xCC, 0x33, 0x77, 0xEE, 0xBB, 0xDD, 0x5A, 0xA5, 0x3C, 0xC3,
   0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0xF0, 0xF0, 0x0F, 0x0F,
   0xCC, 0x33, 0xCC, 0x33, 0x77, 0xEE, 0xBB, 0xDD, 0x5A, 0xA5, 0x3C, 0xC3,
   0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0xF0, 0xF0, 0x0F, 0x0F,
   0xCC, 0x33, 0xCC, 0x33, 0x77, 0xEE, 0xBB, 0xDD, 0x5A, 0xA5, 0x3C, 0xC3,
   0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA, 0xF0, 0xF0, 0x0F, 0x0F,
   0xCC, 0x33, 0xCC, 0x33, 0x77, 0xEE, 0xBB, 0xDD, 0x5A, 0xA5, 0x3C, 0xC3,
   0x55, 0x55, 0x55, 0x55, 0xAA, 0xAA, 0xAA, 0xAA };

static void draw(GLuint pr, int stip)
{
   float pal[16];
   int i;
   glUseProgram(pr);
   for (i = 0; i < 16; i++)
      pal[i] = i * 0.13f - 0.7f;
   glUniform4fv(glGetUniformLocation(pr, "pal"), 4, pal);
   glUniform1i(glGetUniformLocation(pr, "smp"), 0);
   glUniform4f(glGetUniformLocation(pr, "u"), 0.3f, -0.6f, 1.2f, 0.1f);
   glUniform1f(glGetUniformLocation(pr, "t"), 0.7f);
   if (stip)
      glEnable(GL_POLYGON_STIPPLE);
   else
      glDisable(GL_POLYGON_STIPPLE);
   glClearColor(.1f, .2f, .3f, .4f);
   glClear(GL_COLOR_BUFFER_BIT);
   glBegin(GL_TRIANGLES);
   glColor4f(1, 0, 0.2f, 1); glNormal3f(-0.5f, 0.2f, 0.8f); glTexCoord4f(-1, 0, 0.5f, 1); glVertex2f(-1, -1);
   glColor4f(0, 1, 0.7f, 1); glNormal3f(0.6f, -0.3f, 0.7f); glTexCoord4f(4, -1, 0, 2); glVertex2f(1, -1);
   glColor4f(0.3f, 0.2f, 1, 0); glNormal3f(0.1f, 0.9f, 0.4f); glTexCoord4f(1, 3, -2, 0.5f); glVertex2f(-1, 1);
   glColor4f(0.5f, 0.5f, 0.5f, 1); glNormal3f(0.0f, 0.0f, 1.0f); glTexCoord4f(2, 2, 2, 2); glVertex2f(1, 1);
   glColor4f(0, 1, 0.7f, 1); glNormal3f(0.6f, -0.3f, 0.7f); glTexCoord4f(4, -1, 0, 2); glVertex2f(1, -1);
   glColor4f(0.3f, 0.2f, 1, 0); glNormal3f(0.1f, 0.9f, 0.4f); glTexCoord4f(1, 3, -2, 0.5f); glVertex2f(-1, 1);
   glEnd();
   glFinish();
}

/* draw with the JIT off (bufA) and on (bufB) */
static void draw_both(GLuint pr, int stip)
{
   const int v = getenv("GLSL_JIT_SHADER") != NULL;
   _mesa_ro_jit_state = 0;
   OSMesaMakeCurrent(ctx, bufA, GL_UNSIGNED_BYTE, W, H);
   if (v)
      printf("interpreter:\n");
   draw(pr, stip);
   if (v)
      printf("JIT:\n");
   _mesa_ro_jit_state = getenv("GLSL_JIT_NONE") ? 0 : 1;
   OSMesaMakeCurrent(ctx, bufB, GL_UNSIGNED_BYTE, W, H);
   draw(pr, stip);
}

/* largest channel difference; *big = pixels differing by more than 2 */
static int compare(int *big)
{
   int i, c, m = 0;
   *big = 0;
   for (i = 0; i < W * H; i++) {
      int pm = 0;
      for (c = 0; c < 4; c++) {
         int d = abs((int) bufA[i * 4 + c] - (int) bufB[i * 4 + c]);
         if (d > pm)
            pm = d;
      }
      if (pm > m)
         m = pm;
      if (pm > 2)
         (*big)++;
   }
   return m;
}

static GLuint link(const char *fsrc)
{
   GLuint fs = shader(GL_FRAGMENT_SHADER, fsrc), pr;
   GLint ok;
   if (!fs)
      return 0;
   pr = glCreateProgram();
   glAttachShader(pr, vs);
   glAttachShader(pr, fs);
   glLinkProgram(pr);
   glDeleteShader(fs);
   glGetProgramiv(pr, GL_LINK_STATUS, &ok);
   if (!ok) {
      glDeleteProgram(pr);
      return 0;
   }
   return pr;
}

int main(int argc, char **argv)
{
   static char line[200000];
   const char *file = argc > 1 ? argv[1] : "jit-shaders.txt";
   const int limit = argc > 2 ? atoi(argv[2]) : 1000000;
   int fail = 0, i;

   setvbuf(stdout, 0, _IONBF, 0);
   if (&_mesa_ro_jit_state == NULL) {
      printf("glsl-jit: this library has no JIT\nPASS\n");
      return 0;
   }
   ctx = OSMesaCreateContextExt(OSMESA_RGBA, 0, 0, 0, NULL);
   OSMesaMakeCurrent(ctx, bufA, GL_UNSIGNED_BYTE, W, H);
   buf = bufA;
   vs = shader(GL_VERTEX_SHADER, VS);
   glPolygonStipple(stipple);
   {
      static unsigned char im[16 * 16 * 4];
      GLuint tx;
      for (i = 0; i < 16 * 16 * 4; i++)
         im[i] = (unsigned char) ((i * 53 + (i >> 6) * 7) & 255);
      glGenTextures(1, &tx);
      glBindTexture(GL_TEXTURE_2D, tx);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, im);
   }

   /* typical shaders */
   for (i = 0; i < (int) (sizeof(TYPICAL) / sizeof(TYPICAL[0])); i++) {
      GLuint pr = link(TYPICAL[i].fs);
      int st, worst = 0, bigs = 0, ok;
      if (!pr) {
         printf("%-11s does not compile\n", TYPICAL[i].name);
         fail = 1;
         continue;
      }
      for (st = 0; st < 2; st++) {
         int big, m;
         draw_both(pr, st);
         m = compare(&big);
         if (m > worst)
            worst = m;
         bigs += big;
      }
      ok = TYPICAL[i].edge ? bigs <= 4 : worst <= 2;
      printf("%-11s largest difference %3d, pixels over 2: %d%s\n", TYPICAL[i].name,
             worst, bigs, ok ? "" : "  <- too much");
      if (!ok)
         fail = 1;
      glUseProgram(0);
      glDeleteProgram(pr);
   }

   /* random shaders */
   {
      FILE *f = fopen(file, "r");
      long pixels = 0, bigs = 0, same = 0, n = 0, nofit = 0, unstable = 0;
      int worstShader = -1;
      double worstFrac = 0;
      if (!f) {
         printf("glsl-jit: can't open %s\nFAIL\n", file);
         return 1;
      }
      while (n < limit && fgets(line, sizeof line, f)) {
         char *p, *q;
         GLuint pr;
         int big, m;
         for (p = q = line; *p; p++) {
            if (p[0] == '\\' && p[1] == 'n') {
               *q++ = '\n';
               p++;
            }
            else if (*p != '\n')
               *q++ = *p;
         }
         *q = 0;
         if (!line[0])
            continue;
         if (getenv("GLSL_JIT_SHADER") && atoi(getenv("GLSL_JIT_SHADER")) != n) {
            n++;
            continue;
         }
         pr = link(line);
         if (!pr) {
            nofit++;
            n++;
            continue;
         }
         if (getenv("GLSL_JIT_VERBOSE"))
            printf("shader %ld\n", n);
         draw_both(pr, (int) (n & 1));
         m = compare(&big);
         if (m) {
            /* Some random shaders read registers they never write
             * (array elements, say), whose values are left from earlier
             * fragments: then the interpreter doesn't draw the same twice.
             * Leave those out. */
            static unsigned char keep[W * H * 4];
            int dummy;
            memcpy(keep, bufB, sizeof keep);
            _mesa_ro_jit_state = 0;
            OSMesaMakeCurrent(ctx, bufB, GL_UNSIGNED_BYTE, W, H);
            draw(pr, (int) (n & 1));
            if (compare(&dummy)) {
               unstable++;
               glUseProgram(0);
               glDeleteProgram(pr);
               n++;
               continue;
            }
            memcpy(bufB, keep, sizeof keep);
         }
         if (getenv("GLSL_JIT_VERBOSE"))
            printf("  largest %d, over 2: %d\n", m, big);
         if (m == 0)
            same++;
         if ((double) big / (W * H) > worstFrac) {
            worstFrac = (double) big / (W * H);
            worstShader = (int) n;
         }
         bigs += big;
         pixels += W * H;
         glUseProgram(0);
         glDeleteProgram(pr);
         n++;
      }
      fclose(f);
      printf("random: %ld shaders (%ld don't compile, %ld not repeatable), %ld identical; "
             "pixels over 2: %ld of %ld (1 in %ld)", n, nofit, unstable, same, bigs, pixels,
             bigs ? pixels / bigs : 0);
      if (worstShader >= 0)
         printf("; worst: shader %d, %.1f%%", worstShader, worstFrac * 100);
      printf("\n");
      if (bigs * 200 > pixels)
         fail = 1;
   }
   printf(fail ? "FAIL\n" : "PASS\n");
   return 0;   /* the last line is the result */
}
