/*
 * glsl-control.c - riscos-mesa rendering check: shader control flow.
 *
 * GLSL with dynamically indexed uniform arrays (relative addressing),
 * loops with break and continue, nested if/else, discard and
 * gl_FrontFacing; uniforms changed between draws of one program;
 * switching programs mid-frame; and ARB vertex/fragment programs (ARL,
 * LIT, SWZ, KIL, TXP, LRP, CMP), one of them replaced in place with
 * glProgramStringARB.  Prints "<case> <image hash> err<GL error>".
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
#define W 200
#define H 150
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
static GLuint prog(const char *v, const char *f) {
    GLuint p = glCreateProgram();
    GLint ok;
    glAttachShader(p, sh(GL_VERTEX_SHADER, v));
    glAttachShader(p, sh(GL_FRAGMENT_SHADER, f));
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
        printf("LINK FAIL\n");
    return p;
}
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
                glTexCoord2f(b * 2.0f / sl, (a + k) * 2.0f / st);
                glVertex3f(r * x, r * y, r * z);
            }
        }
        glEnd();
    }
}
static void setup(int fr) {
    glClearColor(.1, .1, .2, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.33, 1.33, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -4);
    glRotatef(fr * 25, 0, 1, 0);
}
static const char *VS_ARR =
    "#version 120\nuniform vec4 pal[8]; uniform int k; varying vec4 c; varying vec3 n; varying vec2 tc;\nvoid main(){ int i = int(mod(gl_Vertex.x*7.0+gl_Vertex.y*3.0+10.0, 8.0)); c = pal[i]*0.5 + pal[(i+k)-8*((i+k)/8)]*0.5; n=gl_NormalMatrix*gl_Normal; tc=gl_MultiTexCoord0.xy; gl_Position=ftransform(); }\n";
static const char *VS_LOOP =
    "#version 120\nuniform int iters; varying vec4 c; varying vec3 n; varying vec2 tc;\nvoid main(){ vec3 p=gl_Vertex.xyz; float s=0.0; for(int i=0;i<16;i++){ if(i>=iters) break; s+=sin(p.x*float(i)+p.y); if (s>2.0) continue; p.z+=0.01; } c=vec4(abs(sin(s)),abs(cos(s)),0.5,1.0); n=gl_NormalMatrix*gl_Normal; tc=gl_MultiTexCoord0.xy; gl_Position=gl_ModelViewProjectionMatrix*vec4(p,1.0); }\n";
static const char *FS_IF =
    "#version 120\nvarying vec4 c; varying vec3 n; varying vec2 tc; uniform float t; uniform vec3 L[4];\nvoid main(){ vec3 N=normalize(n); vec3 acc=vec3(0.0); for(int i=0;i<4;i++){ float d=dot(N,normalize(L[i])); if(d>0.0) acc+=c.rgb*d; else if(d>-0.3) acc+=vec3(0.1,0.0,0.0); else acc.b+=0.05; } if (fract(tc.y*5.0+t)<0.08) discard; vec3 m=max(acc,vec3(0.0)); gl_FragColor=vec4(clamp(m,0.0,1.0), 1.0) * (gl_FrontFacing ? 1.0 : 0.5); }\n";
static const char *FS_ARR =
    "#version 120\nvarying vec4 c; varying vec3 n; varying vec2 tc; uniform float w[5]; uniform sampler2D s;\nvoid main(){ int j=int(clamp(tc.x*5.0,0.0,4.0)); vec4 tx=texture2D(s,tc*vec2(w[j],2.0)); gl_FragColor=mix(c,tx,w[4-j]) + vec4(pow(max(n.z,0.0),8.0)); }\n";
static const char *ARB_VP =
    "!!ARBvp1.0\nATTRIB pos=vertex.position; PARAM mvp[4]={state.matrix.mvp}; PARAM arr[3]={{1,0,0,1},{0,1,0,1},{0,0,1,1}}; TEMP t; ADDRESS a;\nDP4 result.position.x, mvp[0], pos; DP4 result.position.y, mvp[1], pos; DP4 result.position.z, mvp[2], pos; DP4 result.position.w, mvp[3], pos;\nMUL t, pos.x, 1.4; ADD t, t, 1.5; ARL a.x, t.x; MOV t, arr[a.x]; LIT t.yz, vertex.normal.zyxw; MAD result.color, t, 0.5, vertex.color; SWZ result.texcoord[0], vertex.texcoord[0], x,-y,0,1;\nEND\n";
static const char *ARB_FP =
    "!!ARBfp1.0\nTEMP t,u; TEX t, fragment.texcoord[0], texture[0], 2D; SUB u, t, 0.3; KIL u.xyzx; TXP u, fragment.texcoord[0], texture[0], 2D; LRP t, fragment.color.a, t, u; CMP t.x, -t.y, t.z, 0.5; FRC u, fragment.position.xyxy; MAD result.color, t, fragment.color, u.xyzw;\nEND\n";
int main() {
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
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, im);
    }
    GLuint p1 = prog(VS_ARR, FS_IF), p2 = prog(VS_LOOP, FS_ARR), p3 = prog(VS_ARR, FS_ARR);
    int fr;
    float pal[32];
    int i;
    for (i = 0; i < 32; i++)
        pal[i] = fmod(i * 0.37, 1.0);
    float L[12] = {1, 1, 1, -1, 0.5, 1, 0, -1, 0.3, 0.2, 0.2, -1};
    float w[5] = {1, 2, 3, 0.3, 0.7};
    for (fr = 0; fr < 4; fr++) {
        setup(fr);
        glUseProgram(p1);
        glUniform4fv(glGetUniformLocation(p1, "pal"), 8, pal);
        glUniform1i(glGetUniformLocation(p1, "k"), fr);
        glUniform3fv(glGetUniformLocation(p1, "L"), 4, L);
        glUniform1f(glGetUniformLocation(p1, "t"), fr * 0.3f);
        glPushMatrix();
        glTranslatef(-.7, 0, 0);
        sphere(.9, 20, 14);
        glPopMatrix();
        /* uniform change between draws of the same program */
        glUniform1i(glGetUniformLocation(p1, "k"), fr + 3);
        glUniform1f(glGetUniformLocation(p1, "t"), fr * 0.7f);
        glPushMatrix();
        glTranslatef(.7, 0, 0);
        glFrontFace(fr & 1 ? GL_CW : GL_CCW);
        sphere(.9, 20, 14);
        glFrontFace(GL_CCW);
        glPopMatrix();
        /* switch program mid-frame, then back */
        glUseProgram(p2);
        glUniform1i(glGetUniformLocation(p2, "iters"), fr * 4 + 3);
        glUniform1fv(glGetUniformLocation(p2, "w"), 5, w);
        glUniform1i(glGetUniformLocation(p2, "s"), 0);
        glPushMatrix();
        glTranslatef(0, .6, .3);
        sphere(.6, 16, 10);
        glPopMatrix();
        glUseProgram(p3);
        glUniform4fv(glGetUniformLocation(p3, "pal"), 8, pal);
        glUniform1i(glGetUniformLocation(p3, "k"), 2);
        w[0] += 0.5f;
        glUniform1fv(glGetUniformLocation(p3, "w"), 5, w);
        glPushMatrix();
        glTranslatef(0, -.6, .3);
        sphere(.6, 16, 10);
        glPopMatrix();
        glUseProgram(p1);
        glPushMatrix();
        glTranslatef(0, 0, .8);
        sphere(.3, 12, 8);
        glPopMatrix();
        glFinish();
        printf("glsl fr%d %08x err%x\n", fr, hash(), glGetError());
    }
    glUseProgram(0);
    {
        GLuint ids[2];
        glGenProgramsARB(2, ids);
        glBindProgramARB(GL_VERTEX_PROGRAM_ARB, ids[0]);
        glProgramStringARB(GL_VERTEX_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, strlen(ARB_VP),
                           ARB_VP);
        if (glGetError())
            printf("VP err %s\n", glGetString(GL_PROGRAM_ERROR_STRING_ARB));
        glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, ids[1]);
        glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, strlen(ARB_FP),
                           ARB_FP);
        if (glGetError())
            printf("FP err %s\n", glGetString(GL_PROGRAM_ERROR_STRING_ARB));
        glEnable(GL_VERTEX_PROGRAM_ARB);
        glEnable(GL_FRAGMENT_PROGRAM_ARB);
        for (fr = 0; fr < 3; fr++) {
            setup(fr);
            sphere(1.2, 24, 16);
            /* replace the fragment program's text in place (same object) */
            if (fr == 1) {
                const char *fp2 =
                    "!!ARBfp1.0\nTEMP t; TEX t, fragment.texcoord[0], texture[0], 2D; MUL result.color, t.zyxw, fragment.color;\nEND\n";
                glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
                                   strlen(fp2), fp2);
            }
            glTranslatef(0, 0, .9);
            sphere(.4, 12, 8);
            glFinish();
            printf("arb fr%d %08x err%x\n", fr, hash(), glGetError());
        }
        glDisable(GL_VERTEX_PROGRAM_ARB);
        glDisable(GL_FRAGMENT_PROGRAM_ARB);
    }
    return 0;
}
