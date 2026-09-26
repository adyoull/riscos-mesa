/*
 * glsl-edge.c - riscos-mesa rendering check: shader edge cases.
 *
 * Early return from main with derivatives (dFdx, dFdy, fwidth); per-pixel
 * loop counts with nested loops, break and continue; discard inside a
 * loop with gl_FragDepth; projective and explicit-LOD texturing on a
 * mipmapped texture; and a loop that never ends for some pixels (Mesa
 * stops those after 65536 instructions and prints "Infinite loop
 * detected").  That last case leaves those pixels' colour undefined, so
 * it's printed as "undefined ..." and run.sh doesn't compare it.
 * Usage: glsl-edge [case]   (all five cases by default)
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
#define W 160
#define H 120
static unsigned char buf[W * H * 4];
static unsigned hash(void) {
    unsigned h = 2166136261u;
    int i;
    for (i = 0; i < W * H * 4; i++) {
        h ^= buf[i];
        h *= 16777619u;
    }
    return h;
}
static GLuint sh(GLenum t, const char *s) {
    GLuint o = glCreateShader(t);
    GLint ok;
    glShaderSource(o, 1, &s, 0);
    glCompileShader(o);
    glGetShaderiv(o, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char l[2000];
        glGetShaderInfoLog(o, 2000, 0, l);
        printf("COMPILE FAIL %s\n", l);
    }
    return o;
}
static const char *VS =
    "#version 120\nvarying vec3 n; varying vec4 c; varying vec4 tc;\nvoid main(){ n=gl_NormalMatrix*gl_Normal; c=gl_Color; tc=gl_MultiTexCoord0*vec4(1.0,1.0,1.0,1.0)+vec4(0.0,0.0,0.0,0.5+0.5*gl_Vertex.x*gl_Vertex.x); gl_Position=ftransform(); }\n";
static const char *FS[] = {
    /* early return + derivatives */
    "#version 120\nvarying vec3 n; varying vec4 c; varying vec4 tc;\nvoid main(){ gl_FragColor=vec4(abs(dFdx(tc.x))*40.0, abs(dFdy(tc.y))*40.0, fwidth(c.r)*20.0, 1.0); if (c.g > 0.6) return; gl_FragColor.b = 0.9; }\n",
    /* per-pixel loop counts, nested loops, break/continue */
    "#version 120\nvarying vec3 n; varying vec4 c; varying vec4 tc;\nvoid main(){ int m=int(mod(gl_FragCoord.x,23.0)); float s=0.0; for(int i=0;i<40;i++){ if(i>=m) break; for(int j=0;j<3;j++){ if(j==i-2*(i/2)) continue; s+=0.01*float(j+1); } if (s>0.5 && c.b>0.5) break; } gl_FragColor=vec4(fract(s*3.0), c.g, s, 1.0); }\n",
    /* gl_FragDepth + discard in a loop */
    "#version 120\nvarying vec3 n; varying vec4 c; varying vec4 tc;\nvoid main(){ for(int i=0;i<4;i++){ if (fract(tc.x*8.0+float(i)*0.1)<0.05) discard; } gl_FragDepth = gl_FragCoord.z*0.5 + c.r*0.25; gl_FragColor=c; }\n",
    /* projective and lod texturing, ternaries */
    "#version 120\nvarying vec3 n; varying vec4 c; varying vec4 tc; uniform sampler2D s;\nvoid main(){ vec4 a=texture2DProj(s,tc); vec4 b=texture2D(s,tc.xy*2.0, 1.5); gl_FragColor = (c.r>0.5) ? a*b : mix(a,b,c.g); }\n",
    /* runaway loop for some pixels (falls back), normal for others */
    "#version 120\nvarying vec3 n; varying vec4 c; varying vec4 tc;\nvoid main(){ float x=0.0; while(true){ x+=0.001; if (c.r < 0.7 && x > 0.05) break; } gl_FragColor=vec4(x, c.g, 0.2, 1.0); }\n",
};
static void sphere(float r, int sl, int st) {
    int a, b;
    for (a = 0; a < st; a++) {
        glBegin(GL_TRIANGLE_STRIP);
        for (b = 0; b <= sl; b++) {
            int k;
            for (k = 0; k < 2; k++) {
                double th = M_PI * (a + k) / st, ph = 2 * M_PI * b / sl;
                float x = sin(th) * cos(ph), y = cos(th), z = sin(th) * sin(ph);
                glNormal3f(x, y, z);
                glColor4f(.5 + .5 * x, .5 + .5 * y, .5 + .5 * z, 1);
                glTexCoord4f(b * 2.0f / sl, (a + k) * 2.0f / st, 0, 1);
                glVertex3f(r * x, r * y, r * z);
            }
        }
        glEnd();
    }
}
int main(int argc, char **argv) {
    int f;
    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 24, 8, 0, NULL);
    OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H);
    {
        static unsigned char im[32 * 32 * 4];
        int i;
        for (i = 0; i < 32 * 32 * 4; i++)
            im[i] = (i * 37) & 255;
        GLuint tx;
        glGenTextures(1, &tx);
        glBindTexture(GL_TEXTURE_2D, tx);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, im);
    }
    for (f = 0; f < 5; f++) {
        if (argc > 1 && f != atoi(argv[1]))
            continue;
        GLuint p = glCreateProgram();
        GLint ok;
        glAttachShader(p, sh(GL_VERTEX_SHADER, VS));
        glAttachShader(p, sh(GL_FRAGMENT_SHADER, FS[f]));
        glLinkProgram(p);
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok)
            printf("LINK FAIL %d\n", f);
        glUseProgram(p);
        glClearColor(.1, .1, .2, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-1.33, 1.33, -1, 1, 2, 20);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0, 0, -4);
        glRotatef(20, 1, 1, 0);
        sphere(1.2, 20, 14);
        glTranslatef(.5, .2, .6);
        sphere(.6, 12, 8);
        glFinish();
        printf("%sfs%d %08x err%x\n", f == 4 ? "undefined " : "", f, hash(), glGetError());
    }
    return 0;
}
