# RISC OS EGL programming guide

For riscos-mesa v20.3.5-5 (September 2026). Andrew Youll.

## Overview

`libEGL` gives RISC OS programs the standard Khronos way to set up OpenGL: EGL 1.4 on top of Mesa's software renderer (OSMesa), with desktop OpenGL 2.1 (GLSL 1.20), OpenGL ES 1.1 and OpenGL ES 2.0. You write ordinary EGL and GL code; the library handles Wimp windows, full screen and sprites. Code written for the Raspberry Pi's Khronos stack can keep its DispmanX window code through a compatibility library.

- **Where it comes from:** riscos-mesa, release v20.3.5-3 or later; the standard extensions need v20.3.5-4, OpenGL ES and DispmanX compatibility v20.3.5-5 (devkit `riscos-mesa-devkit-20.3.5-5.tgz`: `lib/libEGL.a`, `lib/libOSMesa.a`, `lib/libbcm_host.a`, `include/EGL/`, `include/GL/`, `include/GLES/`, `include/GLES2/`).
- **Runs on:** RISC OS 5 on ARMv7 with VFP (Raspberry Pi 2, 3, 4), with SharedUnixLibrary and ARMEABISupport loaded. Tested on a Pi 4.
- **Toolchain:** GCCSDK GCC 10 (`arm-riscos-gnueabihf`), static ELF programs.

Compile and link:

```
arm-riscos-gnueabihf-gcc -O2 -mfpu=vfpv4 -mfloat-abi=hard -fstack-clash-protection \
    -I<devkit>/include -c myprog.c
arm-riscos-gnueabihf-gcc -static myprog.o -o myprog,e1f \
    -L<devkit>/lib -lEGL -lOSMesa -lstdc++ -lz -lm
```

`-lEGL` must come before `-lOSMesa`. Always use `-fstack-clash-protection`: GCC 10 programs on RISC OS crash seemingly at random without it. Never pass `-pthread`.

Headers to include:

```c
#define EGL_EGLEXT_PROTOTYPES 1          /* declare the extension functions */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_riscos.h>   /* RISC OS additions */
#include <GL/gl.h>
```

## Core concepts

Every EGL program follows the same five steps: get the display, initialise it, choose a config, create a surface and a context, then make them current and draw.

| EGL object | What it is on RISC OS |
| --- | --- |
| Display | The screen. Only `EGL_DEFAULT_DISPLAY` (0) is accepted. |
| Config | A pixel format: RGBA 8888 plus a depth/stencil combination (0/0, 16/0, 24/0, 24/8), in one of the two 32bpp colour orders. 8 in all. |
| Context | An OpenGL 2.1 compatibility context (all GL state lives here). |
| Surface | Where GL draws: a window surface, a pbuffer (plain memory) or a pixmap (a sprite). |

**Native types** (defined in `EGL/eglplatform.h` when `__riscos__` is set):

| Type | Meaning |
| --- | --- |
| `EGLNativeDisplayType` (int) | `EGL_DEFAULT_DISPLAY` (0) = the screen |
| `EGLNativeWindowType` (int) | A Wimp window handle, or `EGL_RISCOS_SCREEN_WINDOW` (-1) for the whole screen |
| `EGLNativePixmapType` (void *) | A pointer to a 32bpp sprite's header, in any sprite area |

**Colour order.** RISC OS 32bpp modes store pixels as either `0x00BBGGRR` (ModeFlags bit 14 clear, same as sprite type 6) or `0x00RRGGBB` (bit 14 set). Each config belongs to one order; `EGL_NATIVE_VISUAL_ID` tells you which (`EGL_RISCOS_VISUAL_TBGR` = 0, `EGL_RISCOS_VISUAL_TRGB` = 0x4000). Configs matching the current screen mode get the lowest IDs, so the first config `eglChooseConfig` returns is normally the right one.

**The library never calls `Wimp_Poll`.** Your task keeps its own event loop; EGL draws only when you call `eglSwapBuffers` or one of the redraw helpers.

## Quick start: a full screen program

This complete program draws a spinning triangle over the whole screen for 5 seconds (300 frames at 60 Hz), then exits.

```c
#include <stdio.h>
#include <EGL/egl.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/eglext_riscos.h>
#include <GL/gl.h>

int main(void)
{
    static const EGLint cfg_attrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,     /* desktop GL, not the ES default */
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_NONE
    };
    EGLDisplay dpy;
    EGLConfig cfg;
    EGLint n, w, h;
    EGLSurface surf;
    EGLContext ctx;
    int frame;

    /* 1. display */
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL)) return 1;
    eglBindAPI(EGL_OPENGL_API);

    /* 2. config: the first one matches the screen's colour order */
    if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &n) || n < 1) return 1;

    /* 3. surface (the whole screen) and context */
    surf = eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, NULL);
    ctx  = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT) {
        printf("EGL error 0x%x\n", eglGetError());
        return 1;
    }

    /* 4. make current; 1 = wait for vsync on each swap */
    eglMakeCurrent(dpy, surf, surf, ctx);
    eglSwapInterval(dpy, 1);
    eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
    eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
    glViewport(0, 0, w, h);

    /* 5. draw */
    for (frame = 0; frame < 300; frame++) {
        glClearColor(0.1f, 0.1f, 0.3f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glLoadIdentity();
        glRotatef(frame * 2.0f, 0, 0, 1);
        glBegin(GL_TRIANGLES);
        glColor3f(1, 0, 0); glVertex2f(-0.5f, -0.4f);
        glColor3f(0, 1, 0); glVertex2f( 0.5f, -0.4f);
        glColor3f(0, 0, 1); glVertex2f( 0.0f,  0.6f);
        glEnd();
        eglSwapBuffers(dpy, surf);      /* waits for vsync, then shows the frame */
    }

    /* 6. tidy up */
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    return 0;
}
```

Points to note:

- **Ask for `EGL_OPENGL_BIT`.** EGL's default `EGL_RENDERABLE_TYPE` is OpenGL ES. This library's configs support both, but other EGL implementations may not, so desktop GL code should say what it needs.
- **The swap shows the frame.** GL draws into an off-screen sprite; `eglSwapBuffers` waits for vsync (if the swap interval is 1 or more) and copies it to the screen.
- **From the desktop,** this draws over it and doesn't redraw it afterwards. A desktop program should either use a window (next section) or call `Wimp_ForceRedraw` with window handle -1 on exit.

## Desktop programs: GL in a Wimp window

In the desktop you create the window yourself, pass its handle to `eglCreateWindowSurface`, and keep your normal `Wimp_Poll` loop. EGL takes part in two places: `eglSwapBuffers` after each frame, and `eglRedrawWindowRISCOS` on every redraw request.

```c
/* after Wimp_Initialise, Wimp_CreateWindow and Wimp_OpenWindow: */
surf = eglCreateWindowSurface(dpy, cfg, window_handle, NULL);
eglBindAPI(EGL_OPENGL_API);                /* desktop GL (the initial API is ES) */
ctx  = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
eglMakeCurrent(dpy, surf, surf, ctx);

for (;;) {
    int block[64];
    _kernel_swi_regs r;
    r.r[0] = 0;                               /* null events on: animate */
    r.r[1] = (int) block;
    _kernel_swi(Wimp_Poll, &r, &r);
    switch (r.r[0]) {
    case 0: {                                 /* null: draw a frame */
        EGLint w, h;
        eglQuerySurface(dpy, surf, EGL_WIDTH, &w);   /* follows the window size */
        eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
        glViewport(0, 0, w, h);
        draw_scene(w, h);
        eglSwapBuffers(dpy, surf);            /* Wimp_UpdateWindow + plot */
        break;
    }
    case 1:                                   /* Redraw_Window_Request */
        if (!eglRedrawWindowRISCOS(dpy, block)) {
            /* not an EGL window: do your own redraw loop */
        }
        break;
    case 2:                                   /* Open_Window_Request */
        r.r[1] = (int) block;
        _kernel_swi(Wimp_OpenWindow, &r, &r);
        break;
    case 3:                                   /* Close_Window_Request */
        goto done;
    case 17: case 18:                         /* Message_Quit */
        if (block[4] == 0) goto done;
        break;
    }
}
```

**The window surface:**

- It covers the window's **visible area** and stays put when the window scrolls. It follows the window's size: after a resize, the next `eglSwapBuffers` shows the finished frame, then resizes the surface for the following one. Read `EGL_WIDTH`/`EGL_HEIGHT` and set `glViewport` every frame.
- Give the window a **background colour** (not transparent). The Wimp then clears any part of the window the GL image doesn't cover, for example just after a resize.
- A plain GL window doesn't need scroll bars. Without them, the scroll wheel is free for your program (the wheel position is `OS_Pointer 2`).

**Redraws.** When another window is dragged over yours, the Wimp sends Redraw_Window_Request. `eglRedrawWindowRISCOS(dpy, block)` runs the whole `Wimp_RedrawWindow` / `Wimp_GetRectangle` loop and plots the last finished frame of every EGL surface in that window. It returns `EGL_FALSE`, without starting a redraw, if the window has no EGL surfaces.

**Pacing: don't hog the machine.** In a window `eglSwapBuffers` never waits for vsync, because waiting would stop every other task. A program that draws on every null event uses all the CPU it's given, while still multitasking. To limit the frame rate, use `Wimp_PollIdle` with a time a frame ahead instead of `Wimp_Poll`. To draw only when something changes, turn null events off (poll mask bit 0) and call your draw code from the events that change the scene.

## GL views inside a window

To put a fixed-size GL view at a position in a window that has other content (a 3D preview in a tool window, say), give the surface a work area rectangle. It then scrolls with the work area instead of filling the visible area.

```c
static const EGLint view_attrs[] = {
    EGL_WORK_AREA_X_RISCOS,      32,     /* top left corner, work area OS units */
    EGL_WORK_AREA_Y_RISCOS,     -32,     /* (y is usually 0 or negative) */
    EGL_WORK_AREA_WIDTH_RISCOS,  320,    /* size in pixels */
    EGL_WORK_AREA_HEIGHT_RISCOS, 240,
    EGL_NONE
};
EGLSurface view = eglCreateWindowSurface(dpy, cfg, window_handle, view_attrs);
```

- Width and height must both be given; X and Y default to 0.
- A window can have several surfaces, and one context can draw into each in turn: `eglMakeCurrent` the surface, draw, `eglSwapBuffers`, move on. Set `glViewport` each time, since each surface has its own size.
- Work area surfaces are always drawn on top of a visible area surface in the same window, and the library never plots the visible area surface underneath them (that would flash).
- `eglRedrawWindowRISCOS` redraws all of them.

**Mixing GL with your own drawing.** If the window also has text, icons or Draw graphics, run the redraw loop yourself and call `eglPlotSurfaceRISCOS` for each rectangle:

```c
case 1: {                                   /* Redraw_Window_Request */
    int more;
    r.r[1] = (int) block;
    _kernel_swi(Wimp_RedrawWindow, &r, &r);
    more = r.r[0];
    while (more) {
        draw_my_content(block);             /* your own plotting */
        eglPlotSurfaceRISCOS(dpy, view, block);   /* the GL view on top */
        r.r[1] = (int) block;
        _kernel_swi(Wimp_GetRectangle, &r, &r);
        more = r.r[0];
    }
    break;
}
```

`eglSwapBuffers` on such a surface only updates its own rectangle (`Wimp_UpdateWindow` over the view), so the rest of your window is left alone.

## Full screen programs

Use the native window `EGL_RISCOS_SCREEN_WINDOW` (-1). The surface is the size of the current screen mode, and the default method, plotting a sprite after the vsync wait, gives clean frames on the Pi 4.

| Method | How to get it | Behaviour | Pi 4, 1920x1200 |
| --- | --- | --- | --- |
| Sprite plot (default) | no attributes | GL draws into a sprite; the swap waits for vsync and plots it | Clean; ~6 ms per copy |
| Direct | `EGL_RENDER_BUFFER`, `EGL_SINGLE_BUFFER` | GL draws straight into the visible screen; nothing to copy | Fastest (~100 fps for the test cube), but you see the frame being drawn |
| Screen banks (experimental) | `EGL_SCREEN_BANKS_RISCOS`, 2 or 3 | GL draws into a hidden screen bank; the swap switches banks with `OS_Byte 113` | Tears badly: the bank switch isn't applied in step with vsync |

```c
static const EGLint direct[] = { EGL_RENDER_BUFFER, EGL_SINGLE_BUFFER, EGL_NONE };
surf = eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, direct);

EGLint rb;
eglQuerySurface(dpy, surf, EGL_RENDER_BUFFER, &rb);   /* what you actually got */
```

- **Vsync:** `eglSwapInterval(dpy, n)` waits for n vertical syncs (`OS_Byte 19`) on each swap; the default is 1, and 0 means don't wait.
- **Direct and banks need** a 32bpp mode in the config's colour order; otherwise the library falls back to the sprite plot. Check `EGL_RENDER_BUFFER` or `EGL_SCREEN_BANKS_RISCOS` to see what you got.
- **Bank surfaces don't keep their contents** between frames (`EGL_SWAP_BEHAVIOR` is `EGL_BUFFER_DESTROYED`). Setting `EGL_BUFFER_PRESERVED` with `eglSurfaceAttrib` switches back to the sprite plot. The library returns the display to bank 1 when the surface is destroyed and at exit.
- **Mode changes** are picked up at the next swap: the surface takes the new size.
- **From the desktop:** a full screen program started in the desktop draws over it. While it runs the desktop is frozen (unless you poll), and afterwards you should repaint it with `Wimp_ForceRedraw` (window -1, the whole screen). Running as a Wimp task and changing mode with `Wimp_SetMode` is kinder still.

## Off-screen rendering

For rendering that isn't shown straight away (thumbnails, textures, batch rendering, screenshots), use a pbuffer or render into a sprite.

**Pbuffers** are plain memory, up to 4096x4096. Read the result with `glReadPixels`.

```c
static const EGLint pb_attrs[] = { EGL_WIDTH, 256, EGL_HEIGHT, 256, EGL_NONE };
/* choose the config with EGL_SURFACE_TYPE, EGL_PBUFFER_BIT */
EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pb_attrs);
eglMakeCurrent(dpy, pb, pb, ctx);
/* draw, then: */
glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);   /* bottom row first */
```

**Sprites as pixmaps.** GL renders straight into a 32bpp sprite's image, so the result is an ordinary sprite you can plot, save or edit, with no copying.

```c
/* spr = pointer to a 32bpp sprite's header (e.g. from OS_SpriteOp 24, select sprite) */
static const EGLint pm_attrs[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_SURFACE_TYPE, EGL_PIXMAP_BIT,
    EGL_MATCH_NATIVE_PIXMAP, (EGLint) spr,   /* a config in the sprite's colour order */
    EGL_NONE
};
eglChooseConfig(dpy, pm_attrs, &cfg, 1, &n);
EGLSurface ps = eglCreatePixmapSurface(dpy, cfg, spr, NULL);
eglMakeCurrent(dpy, ps, ps, ctx);            /* ctx made from the same cfg */
/* draw, then glFinish() or eglWaitClient() before using the sprite */
```

- The sprite must be 32bpp. A type 6 sprite (or a mode with ModeFlags bit 14 clear) is `0x00BBGGRR`; bit 14 set is `0x00RRGGBB`. A mismatch with the context's config gives `EGL_BAD_MATCH`.
- GL row 0 (the bottom of the GL image) is the sprite's bottom row, so images come out the right way up.
- Don't move or resize the sprite (for example by adding sprites before it in the same area) while the surface exists.
- Pixmap surfaces are single buffered: `eglSwapBuffers` does nothing, and drawing lands in the sprite as it happens.

**Copying out.** `eglCopyBuffers(dpy, surface, spr)` copies any surface (window, pbuffer or pixmap) into a 32bpp sprite, swapping red and blue if the colour orders differ. It's a quick way to take a screenshot of a window surface.

## Choosing configs and creating contexts

Ask only for what you need; `eglChooseConfig` sorts the matches as the EGL spec says, smallest depth and stencil first, and the screen's colour order breaks ties.

| Attribute | Useful values | Notes |
| --- | --- | --- |
| `EGL_RENDERABLE_TYPE` | `EGL_OPENGL_BIT`, `EGL_OPENGL_ES_BIT`, `EGL_OPENGL_ES2_BIT` | Every config supports all three; the default is ES |
| `EGL_SURFACE_TYPE` | `EGL_WINDOW_BIT`, `EGL_PBUFFER_BIT`, `EGL_PIXMAP_BIT` | All configs support all three |
| `EGL_DEPTH_SIZE` | 0, 16, 24 | 3D scenes need 16 or 24 |
| `EGL_STENCIL_SIZE` | 0, 8 | 8 only comes with depth 24 |
| `EGL_MATCH_NATIVE_PIXMAP` | a sprite pointer | Picks the sprite's colour order |
| `EGL_NATIVE_VISUAL_ID` (read it) | 0 or 0x4000 | The config's colour order |

All configs are RGBA 8888, with no multisampling and no caveat.

**Contexts.** After `eglBindAPI(EGL_OPENGL_API)`, `eglCreateContext(dpy, cfg, share, attrs)` makes an OpenGL 2.1 compatibility context. (The initial API is OpenGL ES, as the EGL spec says: without the bind you get an ES 1.1 context.) With `EGL_KHR_create_context` you can ask for a version or profile:

```c
static const EGLint want_21[] = {
    EGL_CONTEXT_MAJOR_VERSION_KHR, 2,
    EGL_CONTEXT_MINOR_VERSION_KHR, 1,
    EGL_NONE
};
ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, want_21);
```

- Asking for GL 3.0 or later, or a core profile, fails with `EGL_BAD_MATCH`. Check for `EGL_NO_CONTEXT` and fall back to 2.1 if your program can.
- The debug flag is accepted (and ignored); robust access fails with `EGL_BAD_MATCH`.
- **Sharing:** pass an existing context as `share` to share textures, display lists and buffer objects between contexts.
- **Surfaces and contexts must match** in colour order (same config family), or `eglMakeCurrent` fails with `EGL_BAD_MATCH`. Depth and stencil belong to the context, so any depth works with any surface of the right order.
- The draw and read surfaces passed to `eglMakeCurrent` must be the same surface.

**Function pointers.** `eglGetProcAddress` returns every EGL function (including the RISC OS ones) and every GL function, including core GL 1.x/2.x entry points (`EGL_KHR_get_all_proc_addresses`).

## OpenGL ES

OpenGL ES uses the native RISC OS EGL exactly as desktop GL does: the same configs, and the same Wimp windows, work area views, full screen, sprites and pbuffers. Only the context differs. Bind the ES API before creating it, and say which version you want with `EGL_CONTEXT_CLIENT_VERSION`: 1 (the default) gives OpenGL ES 1.1, 2 gives OpenGL ES 2.0 with GLSL ES 1.00.

```c
#include <EGL/egl.h>
#include <GLES2/gl2.h>                /* or <GLES/gl.h> for ES 1.1 */

static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
eglBindAPI(EGL_OPENGL_ES_API);
ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
surf = eglCreateWindowSurface(dpy, cfg, wimp_window_handle, NULL);   /* or -1, a sprite ... */
eglMakeCurrent(dpy, surf, surf, ctx);
/* Wimp_Poll loop, eglSwapBuffers and eglRedrawWindowRISCOS as in the desktop section */
```

`tests/glestest.c` is a complete example: ES 1.1 or 2.0 in a desktop window, full screen and into a sprite.

- Link as for desktop GL: `-lEGL -lOSMesa -lstdc++ -lz -lm`. The ES functions (including ES 1.1's `glOrthof`, `glFrustumf` and fixed-point calls) are in libOSMesa.
- ES 3.x gives `EGL_BAD_MATCH`: the renderer lacks what ES 3.0 needs.
- A desktop GL context and an ES context can't share objects.
- One context is current at a time. Making an ES context current releases a desktop GL one, and the other way round.
- **Speed:** ES 1.1 is fixed function and runs as fast as desktop GL. ES 2.0 is all shaders, and shaders run through Mesa's GLSL interpreter, several times slower for the same scene. Keep ES 2.0 programs small, or render at a low resolution and scale up.
- With SDL2, ask for ES the usual way: `SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES)` and a major version of 1 or 2.

## Running existing Raspberry Pi 1–3 programs (DispmanX)

**Not the way to write RISC OS programs.** This layer exists only so that existing programs written for the Raspberry Pi 1–3's Khronos stack run on this backend without rewriting their window set-up; new code uses the native types, as in the previous section. Programs written for the Raspberry Pi's Khronos stack get their window through DispmanX, then use EGL and OpenGL ES. The DispmanX compatibility library (`libbcm_host`) lets that code build and run unchanged in the usual case. Linking it changes nothing for native windows, and one program can use both. The element you create is a rectangle of the screen, and each `eglSwapBuffers` plots the surface there, after the vsync wait, scaled from the source rectangle to the destination.

```c
#include "bcm_host.h"                 /* first: makes the native window a pointer */
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static EGL_DISPMANX_WINDOW_T nativewindow;
uint32_t w, h;
VC_RECT_T dst, src;

bcm_host_init();
graphics_get_display_size(0, &w, &h);             /* the current screen mode */
vc_dispmanx_rect_set(&dst, 0, 0, w, h);            /* whole screen ... */
vc_dispmanx_rect_set(&src, 0, 0, (w / 2) << 16, (h / 2) << 16);   /* ... from half size */
DISPMANX_DISPLAY_HANDLE_T disp = vc_dispmanx_display_open(0);
DISPMANX_UPDATE_HANDLE_T upd = vc_dispmanx_update_start(0);
nativewindow.element = vc_dispmanx_element_add(upd, disp, 0, &dst, 0, &src,
                                               DISPMANX_PROTECTION_NONE, 0, 0, 0);
nativewindow.width = w / 2;
nativewindow.height = h / 2;
vc_dispmanx_update_submit_sync(upd);
surface = eglCreateWindowSurface(dpy, cfg, &nativewindow, NULL);
```

Link with `-lbcm_host -lEGL -lOSMesa -lstdc++ -lz -lm`. Empty `libGLESv2`, `libGLESv1_CM`, `libvcos` and `libvchiq_arm` are supplied, so a Pi link line only needs `-lOSMesa -lstdc++ -lz -lm` added at the end.

- **Include `bcm_host.h` before the EGL headers** (the Pi examples do), or compile with `-DEGL_RISCOS_DISPMANX`. It makes `EGLNativeWindowType` a pointer, as on the Pi; the compiler stops with an error if the order is wrong.
- **Supported:** `bcm_host_init`, `graphics_get_display_size`, opening and closing the display and `vc_dispmanx_display_get_info`, updates (they take effect at once), adding, moving (`vc_dispmanx_element_change_attributes`: destination, source, opacity 0 to hide) and removing elements.
- **When the element is removed or the program exits,** the desktop underneath is redrawn.
- **Not supported:** layers and alpha blending between elements, rotation and flips, DispmanX resources (`vc_dispmanx_resource_*`, 2D images), `vc_dispmanx_vsync_callback`, and other VideoCore services (OpenMAX, MMAL).
- **Window mode (the default in the desktop):** the program's "display" is a desktop window, 640 pixels wide unless `<App>$Display` or `DispmanX$Display` says otherwise. `graphics_get_display_size` reports the window's size, so the program renders that many pixels. libEGL plots into the window, and libbcm_host polls the Wimp after every `eglSwapBuffers`, so the unchanged program multitasks. Closing the window ends it.
- **Full screen** (`<App>$Display` set to `full`, or outside the desktop): as on the Pi, the program paints over the desktop without multitasking until it exits.
- For a new desktop program, use a Wimp window as the native window instead: see the sections above.

`tests/dmxtest.c` is a complete example; the `dmx-*` Obey files in the tests zip run it.

**Programs already built for the Pi's Khronos module** (for example the GCCSDK autobuilder's `!HelloTriangle`) can't use riscos-mesa as they are: they call that module's SWIs through stub libraries linked into them. Rebuild them from source against the riscos-mesa devkit instead. `ports/hello_pi` does exactly that for `hello_triangle` (ES 1.1), `hello_triangle2` (ES 2.0 shaders and a framebuffer object) and `hello_teapot`, and [porting/hello_pi.md](porting/hello_pi.md) is a step-by-step porting guide. The applications are in the release's `riscos-mesa-hello_pi` zip.

**Porting other programs:** [porting/](porting/README.md) has guides, each with a worked port, for Mesa's EGL demos (eglut), the *OpenGL ES 2.0 Programming Guide* samples (esUtil) and SDL 2 GL programs, and a checklist of what changes in every port.

- **Initial API:** the initial EGL API is OpenGL ES, as the EGL spec says and Pi code expects, so it doesn't need `eglBindAPI`.
- **Precision:** fragment shaders with no default float precision compile, using mediump with a warning, as they did on the Pi.
- **Speed:** shaders run on the CPU, so render a shader-heavy program at a reduced size and let the DispmanX source rectangle scale it up.

## Reference: RISC OS additions

All of these are in `EGL/eglext_riscos.h`, under the extension name `EGL_RISCOS_wimp_window` (listed in `EGL_EXTENSIONS`). The values are provisional: they aren't registered with Khronos and may change.

**Constants**

| Name | Value | Use |
| --- | --- | --- |
| `EGL_RISCOS_SCREEN_WINDOW` | -1 | Native window meaning the whole screen |
| `EGL_WORK_AREA_X_RISCOS` | 0x3FF0 | Window surface attribute: left edge, work area OS units |
| `EGL_WORK_AREA_Y_RISCOS` | 0x3FF1 | Window surface attribute: top edge, work area OS units |
| `EGL_WORK_AREA_WIDTH_RISCOS` | 0x3FF2 | Window surface attribute: width in pixels |
| `EGL_WORK_AREA_HEIGHT_RISCOS` | 0x3FF3 | Window surface attribute: height in pixels |
| `EGL_SCREEN_BANKS_RISCOS` | 0x3FF4 | Full screen: banks wanted (0, 2, 3) at creation; banks in use when queried. Experimental |
| `EGL_FLIP_FIRST_RISCOS` | 0x3FFF | Full screen banks: switch before the vsync wait. Diagnostic, may be removed |
| `EGL_RISCOS_VISUAL_TBGR` | 0x0000 | `EGL_NATIVE_VISUAL_ID` of `0x00BBGGRR` configs |
| `EGL_RISCOS_VISUAL_TRGB` | 0x4000 | `EGL_NATIVE_VISUAL_ID` of `0x00RRGGBB` configs |

**Functions**

```c
EGLBoolean eglRedrawWindowRISCOS(EGLDisplay dpy, int *block);
```

Call on Redraw_Window_Request with the `Wimp_Poll` block (window handle at `block[0]`). Runs the complete `Wimp_RedrawWindow` loop and plots every EGL surface in the window. Returns `EGL_FALSE` with `EGL_BAD_NATIVE_WINDOW`, without starting a redraw, if the window has no EGL surfaces.

```c
EGLBoolean eglPlotSurfaceRISCOS(EGLDisplay dpy, EGLSurface surface, const int *block);
```

Plots one window surface's last frame for the current rectangle of a redraw or update loop you are running yourself (`block` as returned by `Wimp_RedrawWindow` / `Wimp_GetRectangle`). Not for full screen surfaces.

Both are also available through `eglGetProcAddress` (`PFNEGLREDRAWWINDOWRISCOSPROC`, `PFNEGLPLOTSURFACERISCOSPROC`).

**Standard EGL 1.4 on RISC OS: behaviour worth knowing**

| Call | RISC OS behaviour |
| --- | --- |
| `eglSwapBuffers` (window) | `Wimp_UpdateWindow` over the surface and plot; never waits for vsync |
| `eglSwapBuffers` (full screen) | Waits for vsync `swap interval` times, then shows the frame |
| `eglSwapBuffers` (pbuffer, pixmap) | No effect |
| `eglSwapInterval` | 0 to 4; applies to the current surface |
| `eglWaitClient`, `eglWaitGL` | `glFinish` |
| `eglWaitNative` | Nothing to do (RISC OS drawing is synchronous) |
| `eglBindAPI` | `EGL_OPENGL_API` or `EGL_OPENGL_ES_API`. The initial API is OpenGL ES, as the EGL spec says, so desktop GL programs must call `eglBindAPI(EGL_OPENGL_API)` before creating a context |
| `eglBindTexImage`, `eglCreatePbufferFromClientBuffer` | Not supported |

## Standard extensions

`eglQueryString(dpy, EGL_EXTENSIONS)` lists the display extensions below;
`eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS)` lists the client ones.
Every function is also returned by `eglGetProcAddress`. To call them
directly, `#define EGL_EGLEXT_PROTOTYPES 1` before including `EGL/egl.h`
and include `EGL/eglext.h`.

| Extension | What it does here |
| --- | --- |
| `EGL_KHR_create_context` | GL version, profile and flags when creating a context (2.1 compatibility is what you get) |
| `EGL_KHR_get_all_proc_addresses`, `EGL_KHR_client_get_all_proc_addresses` | `eglGetProcAddress` returns core functions as well |
| `EGL_KHR_surfaceless_context` | `eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)` works: for loading textures or rendering to framebuffer objects without a window. Giving just one of draw and read is `EGL_BAD_MATCH` |
| `EGL_KHR_fence_sync`, `EGL_KHR_reusable_sync`, `EGL_KHR_wait_sync` | Sync objects. GL runs on the CPU, in order, so a fence is signalled as soon as it's created (the library calls `glFinish`). With one thread nothing can signal a reusable sync while `eglClientWaitSyncKHR` waits, so an unsignalled one returns `EGL_TIMEOUT_EXPIRED_KHR` at once, whatever the timeout |
| `EGL_EXT_buffer_age` | `eglQuerySurface(..., EGL_BUFFER_AGE_EXT, ...)` on the current surface: 0 = contents unknown (first frame, or the surface was just resized or the mode changed), 1 = the buffer still holds the previous frame (window sprites, full screen sprite, direct rendering), N = the frame from N swaps ago (N screen banks). Pbuffers and pixmaps: 0 |
| `EGL_KHR_swap_buffers_with_damage`, `EGL_EXT_swap_buffers_with_damage` | `eglSwapBuffersWithDamageKHR(dpy, surf, rects, n)`, rectangles x, y, w, h in pixels from the bottom left. In a window only those parts are updated (one `Wimp_UpdateWindow` each; more than 16 become their bounding box); full screen only those parts are plotted after the vsync wait. Screen banks and direct rendering show the whole frame. `n` = 0 is a normal swap |
| `EGL_KHR_partial_update` | After querying the buffer age, `eglSetDamageRegionKHR` says which parts of the surface this frame will change; the next `eglSwapBuffers` then shows only those |
| `EGL_KHR_lock_surface`, `2`, `3` | `eglLockSurfaceKHR` gives direct access to a surface's pixels (a surface that isn't current): query `EGL_BITMAP_POINTER_KHR`, `EGL_BITMAP_PITCH_KHR` (bytes), origin (always `EGL_UPPER_LEFT_KHR`) and the pixel offsets (red at 0, blue at 16 for 0x00BBGGRR configs; the other way round for 0x00RRGGBB), `eglQuerySurface64KHR` for the pointer as an `EGLAttribKHR`. A locked surface can't be made current or swapped. After `eglUnlockSurfaceKHR`, `eglSwapBuffers` shows a window surface written this way even though no context is current to it. `EGL_MATCH_FORMAT_KHR`: 0x00RRGGBB configs are `EGL_FORMAT_RGBA_8888_EXACT_KHR` (B, G, R, A bytes), 0x00BBGGRR ones `EGL_FORMAT_RGBA_8888_KHR` |
| `EGL_KHR_context_flush_control` | `EGL_CONTEXT_RELEASE_BEHAVIOR_KHR` = `EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR` skips the flush when a context stops being current |
| `EGL_KHR_debug` | `eglDebugMessageControlKHR` sets a callback that gets every EGL error with the function name and object labels (`eglLabelObjectKHR`); `eglQueryDebugKHR` reads the settings. Errors and critical messages are on by default |
| `EGL_EXT_client_extensions`, `EGL_EXT_platform_base`, `EGL_RISCOS_platform_wimp` | `eglGetPlatformDisplayEXT(EGL_PLATFORM_RISCOS, NULL, NULL)`. For `eglCreatePlatformWindowSurfaceEXT` the native window is a *pointer to* an int holding the Wimp handle (or -1); for `eglCreatePlatformPixmapSurfaceEXT` it's the sprite pointer. `EGL_PLATFORM_RISCOS` is provisional |
| `EGL_RISCOS_wimp_window` | The RISC OS native types, work area surfaces, full screen options and redraw helpers (see the reference above) |

`egltest -w -D` (Obey file `egl-damage`) is a small example of buffer age
with swap with damage.

## Using the extensions

The biggest saving is redrawing only what changed: with buffer age and a damage swap, a mostly still window costs a fraction of a full frame to show. The other extensions matter mainly to code and libraries ported from elsewhere.

**Redraw only what changed.** Ask for the buffer age each frame. With age 1 the buffer still holds the last frame, so draw only the part that changed and swap with that rectangle. Rectangles are x, y, width, height in pixels from the surface's bottom left, like `glScissor`.

```c
EGLint age = 0, rect[4] = { x, y, w, h };      /* the part that changes */
eglQuerySurface(dpy, surf, EGL_BUFFER_AGE_EXT, &age);
if (age == 1) {                               /* last frame still there */
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect[0], rect[1], rect[2], rect[3]);
    draw_scene();
    glDisable(GL_SCISSOR_TEST);
    eglSwapBuffersWithDamageKHR(dpy, surf, rect, 1);
} else {                                      /* 0: first frame, resized, mode change */
    draw_scene();
    eglSwapBuffers(dpy, surf);
}
```

- In a window each rectangle is one `Wimp_UpdateWindow`; more than 16 are merged into their bounding box.
- Full screen, each rectangle is plotted after the vsync wait. Screen banks and direct rendering always show the whole frame.
- The age is 0 again after the window is resized or the screen mode changes: redraw everything then.
- With `EGL_KHR_partial_update`, call `eglSetDamageRegionKHR` after the age query and before drawing; a plain `eglSwapBuffers` then shows only that region. It's `EGL_BAD_ACCESS` if you didn't query the age this frame or call it twice.
- `egltest -w -D` (Obey file `egl-damage`) is a complete example: the edges keep the first frame while the middle changes colour.

**Sync objects.** Fences and server waits exist so code written for GPUs runs unchanged. Here a fence is already signalled when you get it.

```c
EGLSyncKHR fence = eglCreateSyncKHR(dpy, EGL_SYNC_FENCE_KHR, NULL);  /* needs a current context */
eglClientWaitSyncKHR(dpy, fence, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR);
                                              /* -> EGL_CONDITION_SATISFIED_KHR */
eglDestroySyncKHR(dpy, fence);
```

A reusable sync (`EGL_SYNC_REUSABLE_KHR`) starts unsignalled and changes with `eglSignalSyncKHR`. Everything runs on one thread, so nothing can signal it while you wait: an unsignalled wait returns `EGL_TIMEOUT_EXPIRED_KHR` at once, whatever the timeout.

**No surface needed.** Make a context current with no surface to load textures or build framebuffer objects before a window exists.

```c
eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);   /* both, or neither */
/* ... glTexImage2D, glGenFramebuffersEXT ... */
eglMakeCurrent(dpy, win, win, ctx);    /* viewport and scissor start at the window's size */
```

Drawing to framebuffer 0 while surfaceless goes into a 1x1 stand-in buffer and is lost.

**Pixel access.** Lock a surface that isn't current to read or write its pixels directly: for software rendering of your own, or a video frame.

```c
EGLint ptr, pitch;
eglLockSurfaceKHR(dpy, surf, NULL);           /* surf must not be current */
eglQuerySurface(dpy, surf, EGL_BITMAP_POINTER_KHR, &ptr);
eglQuerySurface(dpy, surf, EGL_BITMAP_PITCH_KHR, &pitch);       /* bytes per row */
/* rows run top down; 32 bits a pixel */
eglUnlockSurfaceKHR(dpy, surf);
eglSwapBuffers(dpy, surf);                    /* shows it, no context needed */
```

- Red is at bit 0 and blue at bit 16 for `0x00BBGGRR` configs, the other way round for `0x00RRGGBB`. Query `EGL_BITMAP_PIXEL_RED_OFFSET_KHR` and friends rather than assuming.
- In `eglChooseConfig`, `EGL_MATCH_FORMAT_KHR` = `EGL_FORMAT_RGBA_8888_EXACT_KHR` picks the `0x00RRGGBB` configs; `EGL_FORMAT_RGBA_8888_KHR` matches all of them.
- A locked surface can't be made current or swapped (`EGL_BAD_ACCESS`).

**Error reporting.** A debug callback gets every EGL error with the function that raised it, so you needn't check `eglGetError` after each call while developing.

```c
static void EGLAPIENTRY on_egl_error(EGLenum error, const char *command, EGLint type,
                                     EGLLabelKHR thread, EGLLabelKHR object, const char *msg)
{
    log_printf("%s: %s\n", command, msg);      /* e.g. "eglMakeCurrent: EGL_BAD_MATCH" */
}

eglDebugMessageControlKHR(on_egl_error, NULL);  /* errors and critical messages */
eglLabelObjectKHR(dpy, EGL_OBJECT_SURFACE_KHR, surf, "main view");   /* shows up as object */
```

Write to a file, not stdout: printing from a Wimp task opens a command window.

**Platform displays.** Code that uses `eglGetPlatformDisplayEXT` (SDL, GLFW-style libraries) works with the RISC OS platform. The native window is a *pointer to* the Wimp handle; the pixmap is the sprite pointer as usual.

```c
int handle = window_handle;                   /* or -1 for the whole screen */
EGLDisplay dpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_RISCOS, NULL, NULL);
EGLSurface s = eglCreatePlatformWindowSurfaceEXT(dpy, cfg, &handle, NULL);
```

**Flush control.** Creating a context with `EGL_CONTEXT_RELEASE_BEHAVIOR_KHR`, `EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR` skips the `glFlush` when you switch away from it. With software GL the saving is small.

## Limits, performance and troubleshooting

Rendering is Mesa's software rasteriser on one CPU core, so keep scenes simple and resolutions modest: fixed-function GL 1.x at 320x240 to 640x480 is where programs stay smooth.

**Pi 4 measurements (RISC OS 5, `egltest`)**

| Case | Frame rate | Render / present per frame |
| --- | --- | --- |
| Lit cube, 640x480 desktop window | ~210 fps | 1.3 ms / 3.1 ms |
| Same, plus a 160x120 GL view in the window | ~170 fps | 1.3 ms / 4.4 ms |
| Lit cube, full screen 1920x1200, vsync | 60 fps | 9.2 ms / waits for vsync |
| Same, no vsync | ~67 fps | 9.2 ms / 5.7 ms |
| Same, direct to screen | ~100 fps | 9.9 ms / 0 ms |

From the riscos-mesa benchmark at 640x480 (ms per frame): clear 1.5, lit cube 5.6, 12k lit triangles 27, 4x blended quads 28, full-screen texture 45, GLSL cube 95. **GLSL is slow**: fixed-function GL is several times faster for the same result.

**Limits**

- OpenGL 2.1, OpenGL ES 1.1 and ES 2.0; GL 3.x, core profiles and ES 3.x are refused.
- One thread: make all EGL and GL calls from the same thread.
- OSMesa can't really release a context: after `eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)` don't make GL calls.
- No multisampling, no bind-to-texture, no EGL 1.5 entry points.
- Static linking only; each program carries its own copy of Mesa (about 9 MB).

**Troubleshooting**

| Symptom | Cause and fix |
| --- | --- |
| `eglChooseConfig` returns no configs | Check what you asked for: all configs are 32-bit RGBA with depth 0, 16 or 24 and no multisampling |
| `eglCreateContext` returns `EGL_NO_CONTEXT`, `EGL_BAD_MATCH` | You asked for GL 3.x or core; ask for 2.1 or nothing |
| `eglMakeCurrent` fails, `EGL_BAD_MATCH` | Surface and context configs are in different colour orders, or draw and read differ |
| Random crashes (abort on data transfer, illegal instruction) | Compile everything with `-fstack-clash-protection` |
| Image in a window doesn't come back after another window covers it | Call `eglRedrawWindowRISCOS` on Redraw_Window_Request |
| Colours swapped (red and blue) | A sprite or config in the other colour order: use `EGL_MATCH_NATIVE_PIXMAP`, or check `EGL_NATIVE_VISUAL_ID` |
| Full screen tears | Use the default sprite plot with a swap interval of 1; direct rendering and (on the Pi 4) screen banks tear |
| Desktop left covered after a full screen run | `Wimp_ForceRedraw` with window -1 before exiting |
| Printing from a Wimp task pops up a window | Normal for UnixLib programs: write results to a file instead |

**A Pi 4 quirk the library works around:** small sprites (well under 1 MB) plotted repeatedly kept showing their first image, black or a frozen frame, even though their memory had changed. Window surface sprites are therefore padded to at least 1 MB with rows that are never shown. If your own code plots small sprites that change every frame, it may need the same treatment.

**Licences.** Programs built with the devkit contain Mesa, UnixLib and the GCC runtime (and SDL, GLU, zlib if used). Ship `LICENCES.txt` from the devkit with them. UnixLib includes LGPL v2 code: open source programs are fine as they are; a closed source program must offer its object files so it can be relinked.

Further reading: `egl/README.md` in the riscos-mesa repository, and `tests/egltest.c`, a complete example of every surface type.
