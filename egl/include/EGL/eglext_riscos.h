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
 *   full screen / single tasking programs. In a 32bpp screen mode in the
 *   config's pixel order it renders straight into screen memory: by default
 *   into a hidden screen bank, shown on swap (OS_Byte 113) after the vsync
 *   wait, using 3 banks if screen memory allows, else 2 (query
 *   EGL_SCREEN_BANKS_RISCOS). Bank surfaces don't preserve their contents
 *   (EGL_SWAP_BEHAVIOR is EGL_BUFFER_DESTROYED; setting EGL_BUFFER_PRESERVED
 *   switches to plotting a sprite). EGL_RENDER_BUFFER = EGL_SINGLE_BUFFER
 *   draws into the visible bank (you see the frame being drawn). Without
 *   enough screen memory, or in other modes, a sprite is plotted on swap.
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
/* eglQuerySurface: screen banks a full screen surface flips between (0 = none) */
#define EGL_SCREEN_BANKS_RISCOS         0x3FF4
/* EXPERIMENTAL, may go: full screen banks switch before the vsync wait
   instead of after (for finding out when the display applies a switch) */
#define EGL_FLIP_FIRST_RISCOS           0x3FFF

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
