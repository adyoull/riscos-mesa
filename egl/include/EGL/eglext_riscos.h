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
 *   view inside a window that has other content.
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
/* EXPERIMENTAL, may go: full screen banks switch before the vsync wait
   instead of after (for finding out when the display applies a switch) */
#define EGL_FLIP_FIRST_RISCOS           0x3FFF
/* EXPERIMENTAL, may go: how a window surface is plotted (0 SpriteOp 34,
   1 clean its cache range first, 2 SpriteOp 52, 3 whole-cache sync first) */
#define EGL_PLOT_METHOD_RISCOS          0x3FFE

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

#ifdef __cplusplus
}
#endif

#endif /* EGLEXT_RISCOS_H */
