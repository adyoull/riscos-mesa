/*
 * perf.c - riscos-mesa performance check: glbench's scenes for a fixed
 * number of frames, for counting the instructions Mesa executes
 * (perf.sh runs it under valgrind). Instruction counts, unlike times, are
 * the same from run to run and on a busy machine, so a slowdown of a few
 * percent shows up reliably.
 *
 * Usage: perf SCENE FRAMES
 *   SCENE: clear, cube, tex, blend, tris, glsl or fog (as in glbench), or
 *   startup: create and bind the context, then stop (FRAMES ignored)
 *   Renders at 320x240 with 24-bit depth and 8-bit stencil, like
 *   glbench's defaults but smaller, so valgrind is quick.
 * Part of riscos-mesa, MIT licence.
 */
#define GLBENCH_NO_MAIN
#include "../../glbench.c"

int main(int argc, char **argv)
{
    OSMesaContext ctx;
    unsigned char *buffer;
    const char *scene;
    void (*frame)(int) = NULL;
    int frames, i;

    if (argc < 3) {
        fprintf(stderr, "usage: perf SCENE FRAMES\n");
        return 1;
    }
    scene = argv[1];
    frames = atoi(argv[2]);
    W = 320;
    H = 240;

    ctx = OSMesaCreateContextExt(getenv("PERF_BGRA") ? OSMESA_BGRA : OSMESA_RGBA, 24, 8, 0, NULL);
    buffer = malloc((size_t)W * H * 4);
    if (!ctx || !buffer || !OSMesaMakeCurrent(ctx, buffer, GL_UNSIGNED_BYTE, W, H)) {
        fprintf(stderr, "OSMesa setup failed\n");
        return 1;
    }
    OSMesaPixelStore(OSMESA_Y_UP, 0);

    if (!strcmp(scene, "startup"))
        return 0;

    reset_state();
    if (!strcmp(scene, "clear"))       frame = s_clear;
    else if (!strcmp(scene, "cube"))  { lit_setup();   frame = s_cube; }
    else if (!strcmp(scene, "tex"))   { tex_setup();   frame = s_tex; }
    else if (!strcmp(scene, "texrgb"))  { tex_rgb = 1; tex_setup(); frame = s_tex; }
    else if (!strcmp(scene, "texfast")) { tex_rgb = tex_fastest = 1; tex_setup(); frame = s_tex; }
    else if (!strcmp(scene, "game"))    { game_setup(); frame = s_game; }
    else if (!strcmp(scene, "torcs"))   { torcs_setup(); frame = s_torcs; }
    else if (!strcmp(scene, "stretch")) { stretch_setup(); frame = s_stretch; }
    else if (!strcmp(scene, "blend")) { blend_setup(); frame = s_blend; }
    else if (!strcmp(scene, "tris"))  { tris_setup();  frame = s_tris; }
    else if (!strcmp(scene, "fog"))   { fog_setup();   frame = s_fog; }
    else if (!strcmp(scene, "glsl")) {
        if (!glsl_setup()) {
            fprintf(stderr, "shader compile failed\n");
            return 1;
        }
        frame = s_glsl;
    }
    if (!frame) {
        fprintf(stderr, "unknown scene %s\n", scene);
        return 1;
    }
    /* one frame outside the count's difference: the first frame of a scene
     * does one-off work (texture upload, state validation) */
    frame(0);
    glFinish();
    for (i = 1; i <= frames; i++) {
        frame(i);
        glFinish();
    }
    OSMesaDestroyContext(ctx);
    return 0;
}
