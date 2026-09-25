/*
 * fg_internal_riscos.h
 *
 * The RISC OS (Wimp) back end of freeglut: private types.
 *
 * Copyright (c) 2026 Andrew Youll. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * How freeglut maps onto RISC OS
 *
 * - A top-level GLUT window is a Wimp window. Its OpenGL context draws into
 *   an EGL window surface (riscos-mesa's native EGL: the surface covers the
 *   window's visible area and follows its size).
 * - A subwindow is a fixed-size EGL work area surface inside its top-level
 *   window (EGL_WORK_AREA_*_RISCOS), with a context of its own.
 * - The task runs the Wimp_Poll loop in glutMainLoopEvent: null events are
 *   taken whenever the program has work (an idle callback, a redisplay, a
 *   timer), so other tasks keep running.
 * - Menus attached with glutAttachMenu are real Wimp menus.
 * - Key releases and mouse motion aren't Wimp events: the keyboard and the
 *   pointer are read on null events.
 */

#ifndef  FREEGLUT_INTERNAL_RISCOS_H
#define  FREEGLUT_INTERNAL_RISCOS_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_riscos.h>

/* -- GLOBAL TYPE DEFINITIONS ---------------------------------------------- */

/* A window "handle" is the Wimp window handle (a subwindow has its
   top-level window's). */
typedef int SFG_WindowHandleType;
typedef EGLContext SFG_WindowContextType;
typedef int SFG_WindowColormapType;             /* no colour index modes */

/* The display: EGL and the Wimp task */
typedef struct tagSFG_PlatformDisplay SFG_PlatformDisplay;
struct tagSFG_PlatformDisplay
{
    EGLDisplay  Display;
    EGLint      MajorVersion, MinorVersion;     /* EGL's version */
    int         Task;                           /* Wimp task handle */
    int         XEig, YEig;                     /* log2 OS units per pixel */
    char        TaskName[64];
};

/* Per window: its EGL surface and config */
typedef struct tagSFG_PlatformContext SFG_PlatformContext;
struct tagSFG_PlatformContext
{
    EGLSurface  Surface;
    EGLConfig   Config;
    int         SurfaceWidth, SurfaceHeight;    /* size of Surface, pixels */
};

/* Per window state */
typedef struct tagSFG_PlatformWindowState SFG_PlatformWindowState;
struct tagSFG_PlatformWindowState
{
    int         Visible[4];     /* top level: visible area x0, y0, x1, y1 (OS units) */
    int         Open;           /* top level: the Wimp window is open */
    int         Borderless;     /* created without title bar and tools */
    int         Normal[4];      /* before full screen: x, y, width, height (pixels) */
    int         Dirty;          /* single-buffered: drawn to since last shown */
    char        Title[256];     /* the title bar (indirected) */
};


/* -- JOYSTICK-SPECIFIC STRUCTURES AND TYPES ------------------------------- */
/* No joystick support on RISC OS (yet) */
#define _JS_MAX_AXES 16
typedef struct tagSFG_PlatformJoystick SFG_PlatformJoystick;
struct tagSFG_PlatformJoystick
{
    int         unused;
};


/* Menus are Wimp menus; these are only for freeglut's own menu code,
   which sizes the entries */
#define  FREEGLUT_MENU_FONT    GLUT_BITMAP_HELVETICA_18

#define  FREEGLUT_MENU_PEN_FORE_COLORS   {0.0f,  0.0f,  0.0f,  1.0f}
#define  FREEGLUT_MENU_PEN_BACK_COLORS   {0.70f, 0.70f, 0.70f, 1.0f}
#define  FREEGLUT_MENU_PEN_HFORE_COLORS  {0.0f,  0.0f,  0.0f,  1.0f}
#define  FREEGLUT_MENU_PEN_HBACK_COLORS  {1.0f,  1.0f,  1.0f,  1.0f}


/* -- Shared between the RISC OS files ------------------------------------ */
struct tagSFG_Window;
struct tagSFG_Menu;

/* fg_window_riscos.c */
struct tagSFG_Window *fghRiscosTopWindow( struct tagSFG_Window *window );
void  fghRiscosClientOrigin( struct tagSFG_Window *window, int *x, int *y );
void  fghRiscosPresent( struct tagSFG_Window *window );
void  fghRiscosReadScreen( void );
void  fghRiscosTakeFocus( struct tagSFG_Window *window );

/* fg_main_riscos.c */
int   fghRiscosPointerHidden( void );
void  fghRiscosUpdatePointerShape( void );

/* fg_menu_riscos.c */
int   fghRiscosOpenMenu( struct tagSFG_Window *window, struct tagSFG_Menu *menu,
                         int x, int y );
void  fghRiscosMenuSelection( const int *items );
void  fghRiscosMenusDeleted( void );

#endif  /* FREEGLUT_INTERNAL_RISCOS_H */
