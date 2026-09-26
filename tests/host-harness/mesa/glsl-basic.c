/*
 * glsl-basic.c - riscos-mesa rendering check: GLSL.
 *
 * Two vertex shaders x two fragment shaders (lighting, a texture, a
 * uniform, gl_FragCoord, pow, discard, blending), three frames each.
 * Prints "<case> <image hash> err<GL error>" per case (see run.sh).
 * Part of riscos-mesa, MIT licence.
 */
#define GL_GLEXT_PROTOTYPES
#include <GL/osmesa.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define W 256
#define H 192
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
    glShaderSource(o, 1, &s, 0);
    glCompileShader(o);
    return o;
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
                glColor3f(.5 + .5 * x, .5 + .5 * y, .5 + .5 * z);
                glTexCoord2f(b * 2.0f / sl, (a + k) * 2.0f / st);
                glVertex3f(r * x, r * y, r * z);
            }
        }
        glEnd();
    }
}
int main() {
    static const char *vs[] = {
        "#version 120\nvarying vec3 n; varying vec3 c; varying vec2 tc;\nvoid main(){ n=gl_NormalMatrix*gl_Normal; c=gl_Color.rgb; tc=gl_MultiTexCoord0.xy; gl_Position=ftransform(); }\n",
        "#version 120\nvarying vec3 n; varying vec3 c; varying vec2 tc; uniform float t;\nvoid main(){ vec4 p=gl_Vertex; p.xyz*=1.0+0.1*sin(p.y*5.0+t); n=normalize(gl_NormalMatrix*gl_Normal); c=abs(n); tc=p.xz; gl_Position=gl_ModelViewProjectionMatrix*p; }\n"};
    static const char *fs[] = {
        "#version 120\nvarying vec3 n; varying vec3 c; varying vec2 tc; uniform float t; uniform sampler2D s;\nvoid main(){ float d=max(dot(normalize(n),normalize(vec3(.4,.6,.8))),0.0); vec4 tx=texture2D(s,tc*3.0); gl_FragColor=vec4(mix(c,tx.rgb,0.4)*(0.25+0.75*d)*(0.7+0.3*sin(gl_FragCoord.y*0.1+t)),1.0); }\n",
        "#version 120\nvarying vec3 n; varying vec3 c; varying vec2 tc; uniform float t;\nvoid main(){ vec3 N=normalize(n); vec3 L=normalize(vec3(1,2,3)); vec3 H=normalize(L+vec3(0,0,1)); float sp=pow(max(dot(N,H),0.0),16.0); if (fract(tc.x*4.0)<0.1) discard; gl_FragColor=vec4(c*max(dot(N,L),0.1)+vec3(sp),0.5+0.5*c.r); }\n"};
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
    int v, f, fr;
    for (v = 0; v < 2; v++)
        for (f = 0; f < 2; f++) {
            GLuint p = glCreateProgram();
            glAttachShader(p, sh(GL_VERTEX_SHADER, vs[v]));
            glAttachShader(p, sh(GL_FRAGMENT_SHADER, fs[f]));
            glLinkProgram(p);
            glUseProgram(p);
            for (fr = 0; fr < 3; fr++) {
                glUniform1f(glGetUniformLocation(p, "t"), fr * 0.7f);
                glEnable(GL_DEPTH_TEST);
                glClearColor(.1, .1, .2, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glMatrixMode(GL_PROJECTION);
                glLoadIdentity();
                glFrustum(-1.33, 1.33, -1, 1, 2, 20);
                glMatrixMode(GL_MODELVIEW);
                glLoadIdentity();
                glTranslatef(0, 0, -4);
                glRotatef(fr * 20, 0, 1, 0);
                if (f == 1) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }
                sphere(1.2, 24, 16);
                glDisable(GL_BLEND);
                glFinish();
                printf("v%d f%d fr%d %08x err%x\n", v, f, fr, hash(), glGetError());
            }
        }
    return 0;
}
