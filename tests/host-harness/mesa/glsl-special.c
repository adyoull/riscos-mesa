/*
 * glsl-special.c - riscos-mesa rendering check: shaders that make
 * denormal numbers, NaNs and infinities.
 *
 * The NEON code in the GLSL interpreter (patch riscos-glsl-neon) flushes
 * denormals to zero and gives the default NaN, where VFP keeps denormals
 * and passes a NaN on with its sign; it hands such instructions back to
 * the C code. These shaders make sure it does: tiny products compared
 * with zero, max/min/step of denormals, dot products of tiny vectors,
 * mod by zero and inf - inf.
 *
 * A NaN's sign decides whether it becomes 0 or 255 in an 8-bit buffer,
 * and which NaN an x86 host makes differs from ARM's, so the NaN cases
 * are printed as "undefined" (not compared with expected/). arm/run-arm.sh
 * also runs this with MESA_NO_NEON=1 and checks every line, those too, is
 * the same with NEON and without.
 * Usage: glsl-special [case]   (all cases by default)
 * Part of riscos-mesa, MIT licence.
 */
#define GL_GLEXT_PROTOTYPES
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
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
    "#version 120\nvarying vec4 c; varying vec4 tc;\nvoid main(){ c=gl_Color; tc=gl_MultiTexCoord0; gl_Position=gl_Vertex; }\n";
#define HEAD "#version 120\nvarying vec4 c; varying vec4 tc; uniform vec4 u;\n"
static const struct { int nan; const char *fs; } FS[] = {
    /* a product too small for a normal float, compared with zero */
    { 0, HEAD "void main(){ float a=c.r*1e-20; float b=a*1e-19; gl_FragColor=vec4(b>0.0?1.0:0.0, b*1e30, a*1e20, 1.0); }\n" },
    /* mod by zero: NaN */
    { 1, HEAD "void main(){ float z=fract(floor(u.y)); float n=mod(c.r, z); gl_FragColor=vec4(-n+0.5, n*c.g, n, 1.0); }\n" },
    /* max, min and step of denormals */
    { 0, HEAD "void main(){ float d=c.g*1e-25*1e-15; gl_FragColor=vec4(max(d,0.0)*1e38, min(-d,0.0)*-1e38, d*1e20*1e20, step(d, 0.0)); }\n" },
    /* dot product and mix of tiny vectors */
    { 0, HEAD "void main(){ vec4 p=c*1e-19; vec4 q=tc*1e-20; float s=dot(p,q); gl_FragColor=vec4(s*1e39, s>0.0?1.0:0.5, mix(p.x,q.y,c.b)*1e38, 1.0); }\n" },
    /* inf - inf: NaN */
    { 1, HEAD "void main(){ float i=1e38*(c.r+1.0)*10.0; float n=i-i; gl_FragColor=vec4(n, -n, n*0.0+c.g, clamp(i, 0.0, 1.0)); }\n" },
};
#define NFS (int) (sizeof(FS) / sizeof(FS[0]))
int main(int argc, char **argv) {
    int f;
    OSMesaContext ctx = OSMesaCreateContextExt(OSMESA_RGBA, 0, 0, 0, NULL);
    OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H);
    for (f = 0; f < NFS; f++) {
        GLuint p;
        GLint ok;
        if (argc > 1 && f != atoi(argv[1]))
            continue;
        p = glCreateProgram();
        glAttachShader(p, sh(GL_VERTEX_SHADER, VS));
        glAttachShader(p, sh(GL_FRAGMENT_SHADER, FS[f].fs));
        glLinkProgram(p);
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok)
            printf("LINK FAIL %d\n", f);
        glUseProgram(p);
        glUniform4f(glGetUniformLocation(p, "u"), 0.3f, -0.6f, 1.2f, 0.1f);
        glClearColor(.1, .2, .3, .4);
        glClear(GL_COLOR_BUFFER_BIT);
        /* a full-screen quad: whole spans, so the batches are full */
        glBegin(GL_QUADS);
        glColor4f(1, 0, 0.2, 1);   glTexCoord4f(-1, 0, 0.5, 1); glVertex2f(-1, -1);
        glColor4f(0, 1, 0.7, 1);   glTexCoord4f(4, -1, 0, 2);   glVertex2f(1, -1);
        glColor4f(0.5, 0.5, 0.5, 1); glTexCoord4f(2, 2, 2, 2);  glVertex2f(1, 1);
        glColor4f(0.3, 0.2, 1, 0); glTexCoord4f(1, 3, -2, 0.5); glVertex2f(-1, 1);
        glEnd();
        glFinish();
        printf("%sfs%d %08x err%x\n", FS[f].nan ? "undefined " : "", f, hash(), glGetError());
        glUseProgram(0);
        glDeleteProgram(p);
    }
    OSMesaDestroyContext(ctx);
    return 0;
}
