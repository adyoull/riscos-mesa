/* r3d_osmesa: OSMesa contexts drawn by RISC3D (osmesa/risc3d_route.c and
   Mesa patch riscos-osmesa-risc3d), end to end with RISC3D's real client
   and Linux helper on the build machine (r3d_e2e.sh builds and runs it).
   The program uses OSMesa as any OSMesa program does; the pictures in its
   buffer must match software's. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/mman.h>
#include <GL/gl.h>
#include <GL/osmesa.h>

#define W 64
#define H 48
static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("ok   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned char a[W * H * 4], b[W * H * 4];

/* blue background, a red triangle over the bottom left half, a green
   quad in front (depth tested) */
static void scene(void)
{
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0);
    glVertex3f(-1, -1, 0.5f); glVertex3f(1, -1, 0.5f); glVertex3f(-1, 1, 0.5f);
    glEnd();
    glBegin(GL_QUADS);
    glColor3f(0, 1, 0);
    glVertex3f(-0.5f, -0.5f, 0.2f); glVertex3f(0, -0.5f, 0.2f);
    glVertex3f(0, 0, 0.2f); glVertex3f(-0.5f, 0, 0.2f);
    glEnd();
    glDisable(GL_DEPTH_TEST);
}

static const unsigned char *px(const unsigned char *buf, int x, int y) { return buf + (y * W + x) * 4; }

static int draw(const int *attribs, unsigned char *buf, const char **renderer)
{
    OSMesaContext c = OSMesaCreateContextAttribs(attribs, NULL);
    if (!c || !OSMesaMakeCurrent(c, buf, GL_UNSIGNED_BYTE, W, H))
        return 0;
    *renderer = (const char *) glGetString(GL_RENDERER);
    *renderer = *renderer ? strdup(*renderer) : "(none)";
    scene();
    glFinish();
    OSMesaMakeCurrent(NULL, NULL, 0, 0, 0);
    OSMesaDestroyContext(c);
    return 1;
}

static void *run(void *arg)
{
    /* RISC3D whatever the build machine draws with (its llvmpipe would
       otherwise be a software driver, which riscos_mesa_pick_renderer
       leaves to our own renderer) */
    setenv("RISC3D$Force", "1", 1);
    static const int rgba[] = { OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 24, 0 };
    static const int soft[] = { OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 24, OSMESA_SOFTWARE_RISCOS, 1, 0 };
    static const int accum[] = { OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 24, OSMESA_ACCUM_BITS, 16, 0 };
    const char *r = NULL;
    int i, n, v;

    /* through RISC3D, and in software for comparison */
    CHECK(draw(rgba, a, &r), "RISC3D: context made and current");
    CHECK(strstr(r, "RISC3D") != NULL, "drawn by RISC3D: %s", r);
    CHECK(draw(soft, b, &r), "OSMESA_SOFTWARE_RISCOS: context made and current");
    CHECK(strstr(r, "OffScreen") != NULL, "OSMESA_SOFTWARE_RISCOS: drawn in software: %s", r);
    for (n = 0, i = 0; i < W * H * 4; i += 4)
        if (abs(a[i] - b[i]) > 8 || abs(a[i + 1] - b[i + 1]) > 8 || abs(a[i + 2] - b[i + 2]) > 8)
            n++;
    CHECK(n * 100 <= W * H * 3, "RISC3D's picture in the buffer matches software's (%d of %d pixels differ)", n, W * H);
    /* OSMesa's rows go bottom up: the red triangle is at the bottom left */
    CHECK(px(a, 2, 2)[0] == 255 && px(a, 2, 2)[2] == 0, "bottom left red (%d %d %d)", px(a, 2, 2)[0], px(a, 2, 2)[1], px(a, 2, 2)[2]);
    CHECK(px(a, W - 3, H - 3)[2] == 255 && px(a, W - 3, H - 3)[0] == 0, "top right blue");
    CHECK(px(a, 20, 18)[1] == 255, "green quad in front (depth test)");

    /* the queries, Y_UP, the colour and depth buffers */
    {
        OSMesaContext c = OSMesaCreateContextAttribs(rgba, NULL);
        GLint w = 0, h = 0, f = 0, bpv = 0;
        void *buf = NULL;
        memset(a, 0, sizeof a);
        CHECK(c && OSMesaMakeCurrent(c, a, GL_UNSIGNED_BYTE, W, H) && OSMesaGetCurrentContext() == c,
              "OSMesaGetCurrentContext: the context drawing through RISC3D");
        OSMesaGetIntegerv(OSMESA_WIDTH, &v); w = v;
        OSMesaGetIntegerv(OSMESA_HEIGHT, &v); h = v;
        CHECK(w == W && h == H, "OSMesaGetIntegerv: %d x %d", w, h);
        OSMesaPixelStore(OSMESA_Y_UP, 0);
        scene();
        CHECK(OSMesaGetColorBuffer(c, &w, &h, &f, &buf) && buf == a && w == W && h == H && f == OSMESA_RGBA,
              "OSMesaGetColorBuffer: the program's buffer, %dx%d", w, h);
        CHECK(px(a, 2, H - 3)[0] == 255 && px(a, W - 3, 2)[2] == 255,
              "OSMESA_Y_UP 0: rows top down (the frame copied in for OSMesaGetColorBuffer)");
        CHECK(OSMesaGetDepthBuffer(c, &w, &h, &bpv, &buf) && w == W && h == H && bpv == 4 &&
              ((unsigned int *) buf)[2 * W + 2] < ((unsigned int *) buf)[(H - 3) * W + W - 3],
              "OSMesaGetDepthBuffer: read back (%u near, %u far)", buf ? ((unsigned int *) buf)[2 * W + 2] : 0,
              buf ? ((unsigned int *) buf)[(H - 3) * W + W - 3] : 0);
        OSMesaMakeCurrent(NULL, NULL, 0, 0, 0);
        CHECK(OSMesaGetCurrentContext() == NULL, "released");
        OSMesaDestroyContext(c);
    }

    /* other formats: BGR, as riscos-mesa's 20.3.5 has it (red first) */
    {
        static const int bgr[] = { OSMESA_FORMAT, OSMESA_BGR, 0 };
        static unsigned char c3[W * H * 3], s3[W * H * 3];
        OSMesaContext c = OSMesaCreateContextAttribs(bgr, NULL);
        static const int bgrs[] = { OSMESA_FORMAT, OSMESA_BGR, OSMESA_SOFTWARE_RISCOS, 1, 0 };
        OSMesaContext s = OSMesaCreateContextAttribs(bgrs, NULL);
        CHECK(c && OSMesaMakeCurrent(c, c3, GL_UNSIGNED_BYTE, W, H), "BGR context current");
        r = (const char *) glGetString(GL_RENDERER);
        CHECK(r && strstr(r, "RISC3D"), "BGR drawn by RISC3D");
        glClearColor(1, 0.5f, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glFinish();
        CHECK(s && OSMesaMakeCurrent(s, s3, GL_UNSIGNED_BYTE, W, H), "BGR software context current");
        glClearColor(1, 0.5f, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glFinish();
        CHECK(!memcmp(c3, s3, 3) && abs(c3[1] - 128) <= 1, "BGR: the same bytes as software (%d %d %d / %d %d %d)",
              c3[0], c3[1], c3[2], s3[0], s3[1], s3[2]);
        OSMesaMakeCurrent(NULL, NULL, 0, 0, 0);
        OSMesaDestroyContext(c);
        OSMesaDestroyContext(s);
    }

    /* what stays in software */
    CHECK(draw(accum, a, &r) && strstr(r, "OffScreen"), "accumulation buffer asked for: software (%s)", r);
    {
        OSMesaContext c1 = OSMesaCreateContextAttribs(soft, NULL);
        OSMesaContext c2 = c1 ? OSMesaCreateContextAttribs(rgba, c1) : NULL;
        CHECK(c2 && OSMesaMakeCurrent(c2, a, GL_UNSIGNED_BYTE, W, H) &&
              (r = (const char *) glGetString(GL_RENDERER)) && strstr(r, "OffScreen"),
              "sharing objects with another context: software (%s)", r ? r : "(none)");
        OSMesaMakeCurrent(NULL, NULL, 0, 0, 0);
        if (c2) OSMesaDestroyContext(c2);
        if (c1) OSMesaDestroyContext(c1);
    }
    setenv("RISC3D$Disable", "1", 1);
    CHECK(draw(rgba, a, &r) && strstr(r, "OffScreen"), "RISC3D$Disable: software (%s)", r);
    unsetenv("RISC3D$Disable");
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
    CHECK(draw(rgba, a, &r) && strstr(r, "OffScreen"), "LIBGL_ALWAYS_SOFTWARE: software (%s)", r);
    unsetenv("LIBGL_ALWAYS_SOFTWARE");
    {
        static float fb[W * H * 4];
        OSMesaContext c = OSMesaCreateContextAttribs(rgba, NULL);
        /* a type RISC3D can't take: software draws it (converting), as before */
        CHECK(c && OSMesaMakeCurrent(c, fb, GL_FLOAT, W, H) && OSMesaGetCurrentContext() == c &&
              (r = (const char *) glGetString(GL_RENDERER)) && strstr(r, "OffScreen"),
              "GL_FLOAT buffer: software's (%s)", r ? r : "(none)");
        OSMesaMakeCurrent(NULL, NULL, 0, 0, 0);
        OSMesaDestroyContext(c);
    }
    printf("%d checks, %d failures: %s\n", checks, failures, failures ? "FAIL" : "ALL PASS");
    (void) arg;
    return NULL;
}

int main(void)
{
    /* As harness.c: allocations and the stack below 2 GB (the fake RISC
       OS's SWIs take 32-bit registers) */
    pthread_attr_t attr;
    pthread_t t;
    size_t stack_size = 8 << 20;
    void *stack;
    mallopt(M_ARENA_MAX, 1);
    mallopt(M_MMAP_MAX, 0);
    mallopt(M_TOP_PAD, 64 << 20);
    stack = mmap((void *) 0x60000000, stack_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (stack == MAP_FAILED) { perror("mmap"); return 2; }
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, stack_size);
    pthread_create(&t, &attr, run, NULL);
    pthread_join(t, NULL);
    return failures != 0;
}
