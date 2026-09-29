/*
 * eglext_riscos.h - RISC OS additions to EGL (riscos-mesa).
 *
 * PROVISIONAL: the extension name, functions and enum values below are not
 * registered with Khronos. The enum values come from the top of the EGL
 * range, which Khronos has not handed out, and may change if a block is
 * registered.
 *
 * EGL_RISCOS_wimp_window
 *   EGLNativeWindowType is a Wimp window handle. By default a window surface
 *   covers the window's visible area (it stays put when the window scrolls)
 *   and follows its size: after the window is resized, the next
 *   eglSwapBuffers shows the finished frame, then resizes the surface for the
 *   next one (query EGL_WIDTH/EGL_HEIGHT to set your viewport).
 *
 *   Giving EGL_WORK_AREA_WIDTH_RISCOS and EGL_WORK_AREA_HEIGHT_RISCOS (pixels)
 *   instead makes a fixed-size surface at a work area position (top left
 *   corner at EGL_WORK_AREA_X_RISCOS, EGL_WORK_AREA_Y_RISCOS, in OS units,
 *   y usually <= 0). It scrolls with the work area, so you can put a GL
 *   view inside a window that has other content. Work area surfaces in a
 *   window stack in creation order (later ones on top).
 *
 *   The library never calls Wimp_Poll: the task owns its event loop. On a
 *   Redraw_Window_Request call eglRedrawWindowRISCOS with the poll block. It
 *   runs the Wimp_RedrawWindow loop and plots every EGL surface in that
 *   window, returning EGL_FALSE (without starting a redraw) if the window
 *   has none. If you draw other things in the same window, run the loop
 *   yourself and call eglPlotSurfaceRISCOS for each rectangle instead.
 *
 *   The native window EGL_RISCOS_SCREEN_WINDOW (-1) is the whole screen, for
 *   full screen / single tasking programs. By default GL renders into a
 *   sprite that eglSwapBuffers plots after the vsync wait.
 *   EGL_SCREEN_BANKS_RISCOS = 2 or 3 at creation (EXPERIMENTAL: tears on the
 *   Pi 4 so far) renders into a hidden screen bank instead, shown on swap
 *   (OS_Byte 113), in a 32bpp mode in the config's pixel order when screen
 *   memory allows (eglQuerySurface gives the number used, 0 = none). Bank
 *   surfaces don't preserve their contents (EGL_SWAP_BEHAVIOR is
 *   EGL_BUFFER_DESTROYED; setting EGL_BUFFER_PRESERVED goes back to the
 *   sprite). EGL_RENDER_BUFFER = EGL_SINGLE_BUFFER draws straight into the
 *   visible screen (no copy; you see the frame being drawn).
 *
 *   Swap interval: full screen, eglSwapBuffers waits for vertical sync
 *   (OS_Byte 19) that many times. In a desktop window it doesn't wait
 *   (that would stop every task); pace frames with Wimp_PollIdle instead.
 *
 *   EGL_NATIVE_VISUAL_ID of a config is the RISC OS ModeFlags colour order
 *   of its pixels: 0 (0x00BBGGRR, as sprite type 6) or 0x4000 (0x00RRGGBB).
 *   The configs matching the current screen mode have the lowest IDs.
 */
#ifndef EGLEXT_RISCOS_H
#define EGLEXT_RISCOS_H

#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EGL_RISCOS_wimp_window
#define EGL_RISCOS_wimp_window 1

#define EGL_RISCOS_SCREEN_WINDOW        ((EGLNativeWindowType) -1)

#define EGL_WORK_AREA_X_RISCOS          0x3FF0
#define EGL_WORK_AREA_Y_RISCOS          0x3FF1
#define EGL_WORK_AREA_WIDTH_RISCOS      0x3FF2
#define EGL_WORK_AREA_HEIGHT_RISCOS     0x3FF3
/* Full screen: screen banks to flip between; creation (0, 2, 3) and query */
#define EGL_SCREEN_BANKS_RISCOS         0x3FF4

/* Hardware overlay for a visible-area window surface (EGL_RISCOS_overlay,
   below): creation attribute, eglSurfaceAttrib and eglQuerySurface. */
#define EGL_OVERLAY_RISCOS              0x3FF6

/* Render size of a visible-area window surface or the full screen,
   scaled to fill the window or screen when shown (EGL_RISCOS_overlay,
   below): creation attributes (both or neither), eglSurfaceAttrib (0 =
   follow the window again) and eglQuerySurface. */
#define EGL_RENDER_WIDTH_RISCOS         0x3FF7
#define EGL_RENDER_HEIGHT_RISCOS        0x3FF8

/* ModeFlags colour order bits reported as EGL_NATIVE_VISUAL_ID */
#define EGL_RISCOS_VISUAL_TBGR          0x0000
#define EGL_RISCOS_VISUAL_TRGB          0x4000

typedef EGLBoolean (EGLAPIENTRYP PFNEGLREDRAWWINDOWRISCOSPROC) (EGLDisplay dpy, int *block);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLPLOTSURFACERISCOSPROC) (EGLDisplay dpy, EGLSurface surface, const int *block);

#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLBoolean EGLAPIENTRY eglRedrawWindowRISCOS (EGLDisplay dpy, int *block);
EGLAPI EGLBoolean EGLAPIENTRY eglPlotSurfaceRISCOS (EGLDisplay dpy, EGLSurface surface, const int *block);
#endif

#endif /* EGL_RISCOS_wimp_window */

/*
 * EGL_RISCOS_overlay
 *   A window surface that covers a window's visible area can be shown
 *   through a hardware overlay (the VideoOverlay module). Opt-in: the
 *   program asks for it (EGL_OVERLAY_RISCOS, below), or the user sets
 *   EGL$Overlay on. Then, when one can be had,
 *   eglSwapBuffers copies the finished frame into an overlay buffer and the
 *   display hardware shows it, switching buffers at vsync, instead of the
 *   frame being plotted into the window. GL still renders into ordinary
 *   memory. Everything else falls back to plotting, as before: VideoOverlay not loaded (load it in !Run with
 *     RMEnsure VideoOverlay 0.00 IfThere System:Modules.VideoOverlay Then RMLoad System:Modules.VideoOverlay
 *   ), no overlay of that size or format, the GPU short of memory, any
 *   error, work area surfaces, full screen and DispmanX surfaces. The
 *   overlay is made once the program is animating (3 swaps in a row, each
 *   within a quarter of a second of the last).
 *
 *   On the Raspberry Pi the overlay sits over the whole desktop, so while
 *   any window or menu overlaps the surface the overlay is hidden and the
 *   frame is plotted instead; it comes back when nothing overlaps. This is
 *   checked at every eglSwapBuffers and eglRedrawWindowRISCOS. A program
 *   that stops swapping for a while (a paused video) should call
 *   eglCheckOverlaysRISCOS(dpy) on null events (a few times a second):
 *   a quarter of a second after the last swap it hides the overlay and
 *   plots the last frame instead, as without an overlay, until the next
 *   eglSwapBuffers, which shows through the overlay again at once.
 *
 *   With an overlay, eglSwapBuffers waits for a vsync when the previous
 *   frame was shown less than a frame ago (the switch happens at the next
 *   vsync, and writing sooner would tear), unless the swap interval is 0.
 *   Full screen surfaces with screen banks likewise wait only until swap
 *   interval vsyncs have passed since the last switch.
 *   eglSwapWouldWaitRISCOS(dpy, surface) says whether a swap right now
 *   would block like that: a program with other work to do (decoding the
 *   next video frame) does it and swaps on its next pass instead.
 *
 *   Render size: EGL_RENDER_WIDTH_RISCOS / EGL_RENDER_HEIGHT_RISCOS (both,
 *   at creation, or either with eglSurfaceAttrib at any time; 0 = follow
 *   the window again) fix the size a visible-area window surface or the
 *   full screen surface renders at; the frame is stretched to fill the
 *   window's visible area or the screen. Through an overlay the display
 *   hardware scales it at no cost; otherwise the sprite plot scales it
 *   (OS_SpriteOp 52). EGL_WIDTH/EGL_HEIGHT report the render size;
 *   pointer positions must be scaled by the program. Full screen with a
 *   render size uses the sprite plot (no direct rendering or banks).
 *
 *   A Basic overlay covers everything on the screen over its rectangle:
 *   anything that should be seen over the picture (a HUD, subtitles,
 *   statistics) must be drawn into the surface. And while any window
 *   overlaps the surface the frames are plotted, which is much slower:
 *   open a program's own windows beside the picture, not over it.
 *
 *   Turning it on and off ("hardware acceleration"):
 *     program:  EGL_OVERLAY_RISCOS = EGL_TRUE in eglCreateWindowSurface's
 *               attributes, or eglSurfaceAttrib(dpy, surface,
 *               EGL_OVERLAY_RISCOS, EGL_TRUE / EGL_FALSE) at any time,
 *               e.g. from a menu option. Without either, no overlay
 *               (unless the user turns them on).
 *     user:     *Set EGL$Overlay on    every program that hasn't said
 *                                      EGL_FALSE ("yes" and "1" too);
 *               *Set EGL$Overlay off   no program, whatever it asked for
 *                                      ("no" and "0" too).
 *   eglQuerySurface(EGL_OVERLAY_RISCOS): 0 not using an overlay, 1 shown
 *   through one, 2 has one but it's hidden (something overlaps the window).
 */
#ifndef EGL_RISCOS_overlay
#define EGL_RISCOS_overlay 1
typedef EGLBoolean (EGLAPIENTRYP PFNEGLCHECKOVERLAYSRISCOSPROC) (EGLDisplay dpy);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLSWAPWOULDWAITRISCOSPROC) (EGLDisplay dpy, EGLSurface surface);
#ifdef EGL_EGLEXT_PROTOTYPES
EGLAPI EGLBoolean EGLAPIENTRY eglCheckOverlaysRISCOS (EGLDisplay dpy);
EGLAPI EGLBoolean EGLAPIENTRY eglSwapWouldWaitRISCOS (EGLDisplay dpy, EGLSurface surface);
#endif
#endif /* EGL_RISCOS_overlay */

/*
 * EGL_RISCOS_platform_wimp (client extension, needs EGL_EXT_platform_base)
 *   eglGetPlatformDisplayEXT(EGL_PLATFORM_RISCOS, NULL, NULL) gives the same
 *   display as eglGetDisplay(EGL_DEFAULT_DISPLAY); native_display must be
 *   NULL. For eglCreatePlatformWindowSurfaceEXT native_window points to an
 *   int holding the Wimp window handle (or -1 for the whole screen). For
 *   eglCreatePlatformPixmapSurfaceEXT native_pixmap is the sprite pointer,
 *   as for eglCreatePixmapSurface.
 */
#ifndef EGL_RISCOS_platform_wimp
#define EGL_RISCOS_platform_wimp 1
#define EGL_PLATFORM_RISCOS             0x3FF5
#endif /* EGL_RISCOS_platform_wimp */

#ifdef __cplusplus
}
#endif

#endif /* EGLEXT_RISCOS_H */
