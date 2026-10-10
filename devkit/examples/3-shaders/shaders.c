/*
 * Example 3: OpenGL ES 2.0 with shaders, in a desktop window.
 *
 * The same spinning triangle as example 2, drawn the modern way. Instead
 * of glBegin/glColor/glVertex, you write two tiny programs ("shaders") in
 * GLSL, a C-like language, and GL runs them for you:
 *   - the vertex shader runs once for each corner and says where it goes;
 *   - the fragment shader runs once for each pixel and says what colour
 *     it is.
 *
 * Why bother? OpenGL ES 2.0 is what phones, the Raspberry Pi's own GPU
 * and WebGL use, so code and tutorials from those worlds work here. The
 * price: shaders run through Mesa's interpreter on the CPU, so they're
 * several times slower than the fixed-function drawing of example 2. For
 * speed on RISC OS, prefer example 2's style; for portability, this one.
 *
 * The desktop part (task, window, poll loop) is the same as example 2,
 * with shorter comments: read that one first.
 *
 * Build: make 3-shaders   (see ../README.md)
 *
 * Part of the riscos-mesa devkit. MIT licence: copy it, change it, use it
 * as the start of your own program.
 */
#define EGL_EGLEXT_PROTOTYPES 1
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext_riscos.h>
#include <GLES2/gl2.h>                   /* ES 2.0, not GL/gl.h */
#include <kernel.h>
#include <swis.h>

#define WIDTH  480
#define HEIGHT 360

static char title[] = "Shaders (OpenGL ES 2.0)";

/* The vertex shader: turns each corner by "angle" and squashes x so the
 * triangle keeps its shape in a wide window. It passes the corner's
 * colour on to the fragment shader, which blends it across the triangle. */
static const char *vertex_source =
    "attribute vec2 position;\n"
    "attribute vec3 colour;\n"
    "uniform float angle;\n"
    "uniform float aspect;\n"
    "varying vec3 v_colour;\n"
    "void main() {\n"
    "    float c = cos(angle), s = sin(angle);\n"
    "    vec2 p = vec2(c * position.x - s * position.y, s * position.x + c * position.y);\n"
    "    gl_Position = vec4(p.x / aspect, p.y, 0.0, 1.0);\n"
    "    v_colour = colour;\n"
    "}\n";

/* The fragment shader: every pixel just takes the blended colour.
 * "precision mediump float" is required in ES fragment shaders. */
static const char *fragment_source =
    "precision mediump float;\n"
    "varying vec3 v_colour;\n"
    "void main() {\n"
    "    gl_FragColor = vec4(v_colour, 1.0);\n"
    "}\n";

/* The triangle: x, y, then red, green, blue, for each corner. */
static const GLfloat corners[] = {
    -0.5f, -0.4f,   1, 0, 0,
     0.5f, -0.4f,   0, 1, 0,
     0.0f,  0.6f,   0, 0, 1,
};

/* Compiles one shader. If your GLSL has a mistake, GL says what and where
 * in the "info log": we print it (RISC OS shows it in a window). */
static GLuint compile(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    GLint ok;
    char log[1024];
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        printf("%s shader didn't compile:\n%s\n",
               type == GL_VERTEX_SHADER ? "Vertex" : "Fragment", log);
        return 0;
    }
    return shader;
}

static int vdu_var(int var)
{
    int in[2] = { var, -1 }, out[1];
    _kernel_swi_regs r;
    r.r[0] = (int) in; r.r[1] = (int) out;
    _kernel_swi(OS_ReadVduVariables, &r, &r);
    return out[0];
}

static int open_window(int scale)  /* as in example 2, where each word is explained */
{
    int w = (WIDTH * scale) << vdu_var(4), h = (HEIGHT * scale) << vdu_var(5), block[23], open[8];
    _kernel_swi_regs r;
    memset(block, 0, sizeof block);
    block[0] = 240; block[1] = 260; block[2] = 240 + w; block[3] = 260 + h;
    block[6] = -1;
    block[7] = (int) 0xAF000002u;
    block[8] = 7 | (2 << 8) | (7 << 16) | (4 << 24);
    block[9] = 3 | (1 << 8) | (12 << 16);
    block[11] = -1024; block[12] = 2048;
    block[14] = 0x07000119;
    block[16] = 1;
    block[18] = (int) title; block[19] = -1; block[20] = sizeof title;
    r.r[1] = (int) block;
    if (_kernel_swi(Wimp_CreateWindow, &r, &r) != NULL)
        return 0;
    open[0] = r.r[0];
    memcpy(&open[1], block, 7 * sizeof(int));
    r.r[1] = (int) open;
    _kernel_swi(Wimp_OpenWindow, &r, &r);
    return open[0];
}

int main(void)
{
    /* Ask for a config that can do OpenGL ES 2.0, and a version 2 context. */
    static const EGLint want[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_NONE
    };
    static const EGLint es2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    static const int messages[] = { 0 };
    EGLDisplay display;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
    EGLint count, width, height;
    GLuint program, vs, fs;
    GLint angle, aspect;
    int task, window, frame = 0, running = 1, block[64];
    EGLint scale_attrs[] = { EGL_WINDOW_SCALE_RISCOS, 1, EGL_NONE };
    unsigned next_frame;
    _kernel_swi_regs r;
    _kernel_oserror *error;

    r.r[0] = 380; r.r[1] = 0x4B534154; r.r[2] = (int) title; r.r[3] = (int) messages;
    error = _kernel_swi(Wimp_Initialise, &r, &r);
    if (error != NULL) {
        /* Usually "Window Manager is currently in use": we were started
         * from a TaskWindow. Say so (in a TaskWindow, printing is fine). */
        printf("%s\n(Start this program by double-clicking its application.)\n",
               error->errmess);
        return 1;
    }
    task = r.r[1];

    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, NULL, NULL);
    eglBindAPI(EGL_OPENGL_ES_API);       /* ES (this is also EGL's default) */
    eglChooseConfig(display, want, &config, 1, &count);
    /* the window scale, as in example 2: 2x2 screen pixels per pixel on a
       high resolution desktop */
    scale_attrs[1] = eglWindowScaleRISCOS(display, WIDTH, HEIGHT);
    if (!(window = open_window(scale_attrs[1])))
        return 1;
    surface = eglCreateWindowSurface(display, config, window, scale_attrs);
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, es2);
    eglMakeCurrent(display, surface, surface, context);

    /* Build the shader program once, at the start: compiling is slow. */
    vs = compile(GL_VERTEX_SHADER, vertex_source);
    fs = compile(GL_FRAGMENT_SHADER, fragment_source);
    if (!vs || !fs)
        return 1;                                  /* the message is printed */
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "position");   /* attribute 0 = x, y */
    glBindAttribLocation(program, 1, "colour");     /* attribute 1 = r, g, b */
    glLinkProgram(program);                        /* join them up */
    {
        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[1024];
            glGetProgramInfoLog(program, sizeof log, NULL, log);
            printf("The shaders didn't link:\n%s\n", log);
            return 1;
        }
    }
    glUseProgram(program);
    angle = glGetUniformLocation(program, "angle");
    aspect = glGetUniformLocation(program, "aspect");

    /* Tell GL where the corners are: 5 floats per corner, position first,
     * colour 2 floats in. */
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), corners);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), corners + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    _kernel_swi(OS_ReadMonotonicTime, &r, &r);
    next_frame = r.r[0];
    while (running) {
        r.r[0] = 0; r.r[1] = (int) block; r.r[2] = next_frame;
        _kernel_swi(Wimp_PollIdle, &r, &r);
        switch (r.r[0]) {
        case 0:                                   /* null: draw a frame */
            eglQuerySurface(display, surface, EGL_WIDTH, &width);
            eglQuerySurface(display, surface, EGL_HEIGHT, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glUniform1f(angle, frame++ * 0.035f);  /* radians: about 2 degrees */
            glUniform1f(aspect, (float) width / height);
            glDrawArrays(GL_TRIANGLES, 0, 3);     /* run the shaders on 3 corners */
            eglSwapBuffers(display, surface);
            next_frame += 2;
            _kernel_swi(OS_ReadMonotonicTime, &r, &r);
            if ((int) (next_frame - r.r[0]) < 0)
                next_frame = r.r[0];
            break;
        case 1:                                   /* redraw */
            eglRedrawWindowRISCOS(display, block);
            break;
        case 2:                                   /* open (move, resize) */
            r.r[1] = (int) block;
            _kernel_swi(Wimp_OpenWindow, &r, &r);
            break;
        case 3:                                   /* close icon */
            running = 0;
            break;
        case 17: case 18:                         /* Message_Quit */
            if (block[4] == 0)
                running = 0;
            break;
        }
    }

    glDeleteProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    block[0] = window;
    r.r[1] = (int) block;
    _kernel_swi(Wimp_DeleteWindow, &r, &r);
    r.r[0] = task; r.r[1] = 0x4B534154;
    _kernel_swi(Wimp_CloseDown, &r, &r);
    return 0;
}
