/*
 * fg_window_riscos.c
 *
 * The RISC OS back end of freeglut: windows (Wimp windows and EGL
 * surfaces), contexts and buffer swaps.
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

#include <kernel.h>
#include <swis.h>
#include <GL/freeglut.h>
#include "../fg_internal.h"

#define Wimp_CreateWindow_      0x400C1
#define Wimp_DeleteWindow_      0x400C3
#define Wimp_OpenWindow_        0x400C5
#define Wimp_CloseWindow_       0x400C6
#define Wimp_GetWindowState_    0x400CB
#define Wimp_ForceRedraw_       0x400D1
#define Wimp_SetCaretPosition_  0x400D2
#define Wimp_GetPointerInfo_    0x400CF
#define Wimp_GetWindowOutline_  0x400E0
#define OS_ReadModeVariable_    0x35
#define OS_Word_                0x07

#define TASK_WORD 0x4B534154            /* "TASK" */

/* Window flags: new format, moveable; back, close, title, toggle, adjust */
#define FLAGS_NORMAL        0xAF000002u
/* No title bar or tools (borderless, full screen and game mode windows) */
#define FLAGS_BORDERLESS    0x80000000u

extern void fghOnReshapeNotify( SFG_Window *window, int width, int height,
                                GLboolean forceNotify );
extern void fghOnPositionNotify( SFG_Window *window, int x, int y,
                                 GLboolean forceNotify );
extern void fghContextCreationError( void );

static int fghWindowsOpened;            /* for staggering default positions */


/* -- Screen ------------------------------------------------------------- */

static int fghModeVariable( int var, int fallback )
{
    _kernel_swi_regs r;
    r.r[0] = -1;
    r.r[1] = var;
    if( _kernel_swi( OS_ReadModeVariable_, &r, &r ) != NULL )
        return fallback;
    return r.r[2];
}

/* Read the screen size and pixel shape (after a mode change too). */
void fghRiscosReadScreen( void )
{
    SFG_PlatformDisplay *d = &fgDisplay.pDisplay;

    d->XEig = fghModeVariable( 4, 1 );
    d->YEig = fghModeVariable( 5, 1 );
    fgDisplay.ScreenWidth  = fghModeVariable( 11, 639 ) + 1;
    fgDisplay.ScreenHeight = fghModeVariable( 12, 479 ) + 1;
    /* 180 OS units to the inch */
    fgDisplay.ScreenWidthMM  = (int) ( fgDisplay.ScreenWidth  * ( 1 << d->XEig ) * 25.4f / 180.0f + 0.5f );
    fgDisplay.ScreenHeightMM = (int) ( fgDisplay.ScreenHeight * ( 1 << d->YEig ) * 25.4f / 180.0f + 0.5f );
}

#define XEIG ( fgDisplay.pDisplay.XEig )
#define YEIG ( fgDisplay.pDisplay.YEig )
#define SCREEN_TOP ( fgDisplay.ScreenHeight << YEIG )


/* -- Window tree --------------------------------------------------------- */

SFG_Window *fghRiscosTopWindow( SFG_Window *window )
{
    while( window && window->Parent )
        window = window->Parent;
    return window;
}

/* Where a window's client area is in its top-level window's, in pixels. */
void fghRiscosClientOrigin( SFG_Window *window, int *x, int *y )
{
    *x = *y = 0;
    while( window && window->Parent )
    {
        *x += window->State.Xpos;
        *y += window->State.Ypos;
        window = window->Parent;
    }
}

/* Read a top-level window's visible area from the Wimp. */
static void fghReadVisibleArea( SFG_Window *window )
{
    int block[9];
    _kernel_swi_regs r;

    block[0] = window->Window.Handle;
    r.r[1] = (int) block;
    if( _kernel_swi( Wimp_GetWindowState_, &r, &r ) == NULL )
        memcpy( window->State.pWState.Visible, &block[1], 4 * sizeof( int ) );
}

/* A top-level window's size and position in GLUT terms (pixels, from the
   top left of the screen) from its visible area. */
static void fghVisibleToGlut( SFG_Window *window, int *x, int *y, int *w, int *h )
{
    const int *v = window->State.pWState.Visible;
    *x = v[0] >> XEIG;
    *y = ( SCREEN_TOP - v[3] ) >> YEIG;
    *w = ( v[2] - v[0] ) >> XEIG;
    *h = ( v[3] - v[1] ) >> YEIG;
}

void fghRiscosTakeFocus( SFG_Window *window )
{
    _kernel_swi_regs r;
    window = fghRiscosTopWindow( window );
    if( !window || !window->State.pWState.Open )
        return;
    r.r[0] = window->Window.Handle;
    r.r[1] = -1;
    r.r[2] = 0;
    r.r[3] = 0;
    r.r[4] = 1 << 25;                   /* invisible caret */
    r.r[5] = -1;
    _kernel_swi( Wimp_SetCaretPosition_, &r, &r );
}

/* Open (or move, resize, restack) a top-level window: x, y, w, h in GLUT
   pixels; behind is -1 (top), -2 (bottom) or -3 (keep its place). */
static void fghOpenWimpWindow( SFG_Window *window, int x, int y, int w, int h, int behind )
{
    int block[8];
    _kernel_swi_regs r;

    if( w < 1 ) w = 1;
    if( h < 1 ) h = 1;
    block[0] = window->Window.Handle;
    block[1] = x << XEIG;
    block[4] = SCREEN_TOP - ( y << YEIG );
    block[3] = block[1] + ( w << XEIG );
    block[2] = block[4] - ( h << YEIG );
    block[5] = 0;
    block[6] = 0;
    if( behind == -3 )
    {
        int state[9];
        state[0] = window->Window.Handle;
        r.r[1] = (int) state;
        behind = -1;
        if( window->State.pWState.Open &&
            _kernel_swi( Wimp_GetWindowState_, &r, &r ) == NULL )
            behind = state[7];
    }
    block[7] = behind;
    r.r[1] = (int) block;
    _kernel_swi( Wimp_OpenWindow_, &r, &r );
    window->State.pWState.Open = 1;
    fghReadVisibleArea( window );
}

/* Create the Wimp window for a top-level GLUT window (not opened). */
static int fghCreateWimpWindow( SFG_Window *window, int borderless )
{
    int wb[23];
    _kernel_swi_regs r;
    int sw = fgDisplay.ScreenWidth << XEIG, sh = SCREEN_TOP;

    memset( wb, 0, sizeof wb );
    wb[0] = 0; wb[1] = 0; wb[2] = 64; wb[3] = 64;  /* opened later */
    wb[6] = -1;
    wb[7] = (int) ( borderless ? FLAGS_BORDERLESS : FLAGS_NORMAL );
    /* title fg black, bg grey; work area fg black, bg black (seen only
       before the first frame); scroll bars; title highlight cream */
    wb[8] = 7 | ( 2 << 8 ) | ( 7 << 16 ) | ( 7 << 24 );
    wb[9] = 3 | ( 1 << 8 ) | ( 12 << 16 );
    /* work area: the size of the screen, so the window can grow that big */
    wb[10] = 0; wb[11] = -sh; wb[12] = sw; wb[13] = 0;
    wb[14] = 0x07000119;                /* title: text, centred, indirected */
    wb[15] = 3 << 12;                   /* button type: click */
    wb[16] = 1;                         /* Wimp sprite area */
    wb[17] = ( 1 << XEIG ) | ( ( 1 << YEIG ) << 16 );  /* minimum size */
    wb[18] = (int) window->State.pWState.Title;
    wb[19] = -1;
    wb[20] = sizeof window->State.pWState.Title;
    wb[21] = 0;                         /* no icons */
    r.r[1] = (int) wb;
    if( _kernel_swi( Wimp_CreateWindow_, &r, &r ) != NULL )
        return 0;
    window->Window.Handle = r.r[0];
    window->State.pWState.Borderless = borderless;
    window->State.pWState.Open = 0;
    return 1;
}

static void fghDeleteWimpWindow( int handle )
{
    int block[1];
    _kernel_swi_regs r;
    block[0] = handle;
    r.r[1] = (int) block;
    _kernel_swi( Wimp_DeleteWindow_, &r, &r );
}

/* Redraw a top-level window's work area (where a subwindow was). */
static void fghForceRedraw( SFG_Window *top )
{
    _kernel_swi_regs r;
    if( !top || !top->State.pWState.Open )
        return;
    r.r[0] = top->Window.Handle;
    r.r[1] = 0;
    r.r[2] = -SCREEN_TOP;
    r.r[3] = fgDisplay.ScreenWidth << XEIG;
    r.r[4] = 0;
    _kernel_swi( Wimp_ForceRedraw_, &r, &r );
}


/* -- EGL ---------------------------------------------------------------- */

static int fghChooseConfig( EGLConfig *config )
{
    EGLint attributes[32], n = 0, num_config = 0;

#define ADD_ATTRIB(a, v) ( attributes[n++] = (a), attributes[n++] = (v) )
    ADD_ATTRIB( EGL_SURFACE_TYPE, EGL_WINDOW_BIT );
#ifdef FREEGLUT_GLES
    ADD_ATTRIB( EGL_RENDERABLE_TYPE, fgState.MajorVersion >= 2 ? EGL_OPENGL_ES2_BIT
                                                               : EGL_OPENGL_ES_BIT );
#else
    ADD_ATTRIB( EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT );
#endif
    ADD_ATTRIB( EGL_RED_SIZE, 1 );
    ADD_ATTRIB( EGL_GREEN_SIZE, 1 );
    ADD_ATTRIB( EGL_BLUE_SIZE, 1 );
    ADD_ATTRIB( EGL_ALPHA_SIZE, ( fgState.DisplayMode & GLUT_ALPHA ) ? 1 : 0 );
    ADD_ATTRIB( EGL_DEPTH_SIZE, ( fgState.DisplayMode & GLUT_DEPTH ) ? 1 : 0 );
    ADD_ATTRIB( EGL_STENCIL_SIZE, ( fgState.DisplayMode & GLUT_STENCIL ) ? 1 : 0 );
    /* No multisampling or accumulation buffers: GLUT_MULTISAMPLE and
       GLUT_ACCUM give an ordinary window (as GLUT does when the display
       can't do them). */
    attributes[n] = EGL_NONE;
#undef ADD_ATTRIB

    if( !eglChooseConfig( fgDisplay.pDisplay.Display, attributes, config, 1, &num_config ) ||
        num_config < 1 )
    {
        *config = NULL;
        return 0;
    }
    return 1;
}

int fghRiscosDisplayModePossible( void )
{
    EGLConfig config;
    return fghChooseConfig( &config );
}

static EGLContext fghCreateContext( SFG_Window *window )
{
    EGLint attributes[16], n = 0;
    EGLContext context;

#ifdef FREEGLUT_GLES
    attributes[n++] = EGL_CONTEXT_CLIENT_VERSION;
    attributes[n++] = fgState.MajorVersion;
#else
    if( fgState.MajorVersion > 1 || fgState.MinorVersion > 0 )
    {
        attributes[n++] = EGL_CONTEXT_MAJOR_VERSION_KHR;
        attributes[n++] = fgState.MajorVersion;
        attributes[n++] = EGL_CONTEXT_MINOR_VERSION_KHR;
        attributes[n++] = fgState.MinorVersion;
    }
    if( fgState.ContextProfile )
    {
        attributes[n++] = EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR;
        attributes[n++] = ( fgState.ContextProfile & GLUT_CORE_PROFILE )
                          ? EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR
                          : EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR;
    }
    if( fgState.ContextFlags & ( GLUT_DEBUG | GLUT_FORWARD_COMPATIBLE ) )
    {
        attributes[n++] = EGL_CONTEXT_FLAGS_KHR;
        attributes[n++] = ( ( fgState.ContextFlags & GLUT_DEBUG ) ? EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR : 0 ) |
                          ( ( fgState.ContextFlags & GLUT_FORWARD_COMPATIBLE ) ? EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR : 0 );
    }
#endif
    attributes[n] = EGL_NONE;

    context = eglCreateContext( fgDisplay.pDisplay.Display, window->Window.pContext.Config,
                                EGL_NO_CONTEXT, attributes );
    if( context == EGL_NO_CONTEXT )
        fghContextCreationError( );
    return context;
}

/* Make a window's surface and context current (again). */
static void fghMakeCurrent( SFG_Window *window )
{
    EGLSurface s = window->Window.pContext.Surface;
    if( window->IsMenu || window->Window.Context == EGL_NO_CONTEXT )
        return;
    if( eglMakeCurrent( fgDisplay.pDisplay.Display, s, s, window->Window.Context ) == EGL_FALSE )
        fgWarning( "eglMakeCurrent: error %x", eglGetError() );
}

/* Create a window's EGL surface: the visible area of a top-level window, or
   a work area rectangle in its top-level window for a subwindow. */
static void fghCreateSurface( SFG_Window *window )
{
    SFG_PlatformContext *pc = &window->Window.pContext;
    SFG_Window *top = fghRiscosTopWindow( window );
    EGLint attributes[10];
    int x, y;

    if( window->IsMenu || pc->Surface != EGL_NO_SURFACE )
        return;
    if( window->Parent )
    {
        fghRiscosClientOrigin( window, &x, &y );
        attributes[0] = EGL_WORK_AREA_X_RISCOS;      attributes[1] = x << XEIG;
        attributes[2] = EGL_WORK_AREA_Y_RISCOS;      attributes[3] = -( y << YEIG );
        attributes[4] = EGL_WORK_AREA_WIDTH_RISCOS;  attributes[5] = window->State.Width  > 0 ? window->State.Width  : 1;
        attributes[6] = EGL_WORK_AREA_HEIGHT_RISCOS; attributes[7] = window->State.Height > 0 ? window->State.Height : 1;
        attributes[8] = EGL_NONE;
    }
    else
        attributes[0] = EGL_NONE;

    pc->Surface = eglCreateWindowSurface( fgDisplay.pDisplay.Display, pc->Config,
                                          (EGLNativeWindowType) top->Window.Handle,
                                          attributes );
    if( pc->Surface == EGL_NO_SURFACE )
        fgError( "Cannot create EGL window surface, err=%x", eglGetError() );
    eglQuerySurface( fgDisplay.pDisplay.Display, pc->Surface, EGL_WIDTH, &pc->SurfaceWidth );
    eglQuerySurface( fgDisplay.pDisplay.Display, pc->Surface, EGL_HEIGHT, &pc->SurfaceHeight );
    if( window == fgStructure.CurrentWindow )
        fghMakeCurrent( window );
}

static void fghDestroySurface( SFG_Window *window )
{
    SFG_PlatformContext *pc = &window->Window.pContext;
    if( pc->Surface == EGL_NO_SURFACE )
        return;
    if( eglGetCurrentSurface( EGL_DRAW ) == pc->Surface )
        eglMakeCurrent( fgDisplay.pDisplay.Display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT );
    eglDestroySurface( fgDisplay.pDisplay.Display, pc->Surface );
    pc->Surface = EGL_NO_SURFACE;
}

/* A window's subwindows (all levels) get new surfaces: after the top-level
   Wimp window was replaced, a subwindow moved or the screen mode changed. */
static void fghRecreateSubSurfaces( SFG_Window *window )
{
    SFG_Window *child;
    for( child = (SFG_Window *) window->Children.First; child;
         child = (SFG_Window *) child->Node.Next )
    {
        int had = child->Window.pContext.Surface != EGL_NO_SURFACE;
        fghDestroySurface( child );
        if( had )
        {
            fghCreateSurface( child );
            child->State.WorkMask |= GLUT_DISPLAY_WORK;
        }
        fghRecreateSubSurfaces( child );
    }
}

/* Replace a top-level window's Wimp window (to add or remove the title bar
   and tools), putting it at x, y, w, h. */
static void fghReplaceWimpWindow( SFG_Window *window, int borderless,
                                  int x, int y, int w, int h )
{
    int old = window->Window.Handle, was_open = window->State.pWState.Open;
    SFG_Window *current = fgStructure.CurrentWindow;

    if( !fghCreateWimpWindow( window, borderless ) )
    {
        fgWarning( "Wimp_CreateWindow failed" );
        return;
    }
    fghDestroySurface( window );
    fghOpenWimpWindow( window, x, y, w, h, -1 );
    fghCreateSurface( window );
    fghRecreateSubSurfaces( window );
    fghDeleteWimpWindow( old );
    (void) was_open;
    if( current )
        fghMakeCurrent( current );
    fghRiscosTakeFocus( window );
    window->State.WorkMask |= GLUT_DISPLAY_WORK;
    fghVisibleToGlut( window, &x, &y, &w, &h );
    fghOnPositionNotify( window, x, y, GL_FALSE );
    fghOnReshapeNotify( window, w, h, GL_FALSE );
}

/* After a screen mode change: new pixel sizes, and new work area surfaces
   (they are placed in OS units). */
void fghRiscosModeChanged( void )
{
    SFG_Window *window;
    int x, y, w, h;

    fghRiscosReadScreen( );
    for( window = (SFG_Window *) fgStructure.Windows.First; window;
         window = (SFG_Window *) window->Node.Next )
    {
        if( window->IsMenu || !window->State.pWState.Open )
            continue;
        fghReadVisibleArea( window );
        fghRecreateSubSurfaces( window );
        fghVisibleToGlut( window, &x, &y, &w, &h );
        fghOnPositionNotify( window, x, y, GL_FALSE );
        fghOnReshapeNotify( window, w, h, GL_FALSE );
        window->State.WorkMask |= GLUT_DISPLAY_WORK;
    }
}

/* A top-level window was moved or resized (Open_Window_Request). */
void fghRiscosWindowOpened( SFG_Window *window )
{
    int x, y, w, h;
    fghReadVisibleArea( window );
    fghVisibleToGlut( window, &x, &y, &w, &h );
    fghOnPositionNotify( window, x, y, GL_FALSE );
    fghOnReshapeNotify( window, w, h, GL_FALSE );
}


/* -- freeglut's window calls -------------------------------------------- */

void fgPlatformCreateWindow( SFG_Window *window )
{
    window->Window.pContext.Surface = EGL_NO_SURFACE;
    window->Window.pContext.Config = NULL;
    window->Window.Context = EGL_NO_CONTEXT;
}

void fgPlatformOpenWindow( SFG_Window* window, const char* title,
                           GLboolean positionUse, int x, int y,
                           GLboolean sizeUse, int w, int h,
                           GLboolean gameMode, GLboolean isSubWindow )
{
    SFG_Window *current = fgStructure.CurrentWindow;

    /* Menus are Wimp menus: their freeglut windows are never shown */
    if( window->IsMenu )
        return;

    if( !fghChooseConfig( &window->Window.pContext.Config ) )
        fgError( "No EGL config for this display mode (%x)", fgState.DisplayMode );

    if( !sizeUse )
    {
        w = 300;
        h = 300;
    }
    window->State.Width = w;
    window->State.Height = h;

    if( fgState.UseCurrentContext && current && !current->IsMenu &&
        current->Window.Context != EGL_NO_CONTEXT )
        window->Window.Context = current->Window.Context;
    else
        window->Window.Context = fghCreateContext( window );

    if( isSubWindow )
    {
        window->State.Xpos = x;
        window->State.Ypos = y;
        window->Window.Handle = fghRiscosTopWindow( window )->Window.Handle;
        fghCreateSurface( window );
        window->State.Visible = GL_TRUE;
        return;
    }

    snprintf( window->State.pWState.Title, sizeof window->State.pWState.Title,
              "%s", title ? title : "" );
    if( !fghCreateWimpWindow( window, gameMode || ( fgState.DisplayMode & GLUT_BORDERLESS ) ) )
        fgError( "Wimp_CreateWindow failed" );

    if( gameMode )
    {
        x = y = 0;
        w = fgDisplay.ScreenWidth;
        h = fgDisplay.ScreenHeight;
        window->State.IsFullscreen = GL_TRUE;
    }
    else if( !positionUse || x < 0 || y < 0 )
    {
        /* centred, each new window a little lower and to the right */
        int step = ( fghWindowsOpened++ % 8 ) * 24;
        x = ( fgDisplay.ScreenWidth - w ) / 2 + step - 84;
        y = ( fgDisplay.ScreenHeight - h ) / 2 + step - 84;
        if( x < 0 ) x = 0;
        if( y < ( 48 >> YEIG ) ) y = 48 >> YEIG;      /* room for the title bar */
    }

    fghOpenWimpWindow( window, x, y, w, h, -1 );
    fghVisibleToGlut( window, &window->State.Xpos, &window->State.Ypos,
                      &window->State.Width, &window->State.Height );
    fghCreateSurface( window );
    window->State.Visible = GL_TRUE;
    fghRiscosTakeFocus( window );
}

void fgPlatformCloseWindow( SFG_Window* window )
{
    SFG_Window *iter, *top;
    int used = 0;

    if( window->IsMenu )
        return;

    top = fghRiscosTopWindow( window );
    if( fgStructure.CurrentWindow == window )
        eglMakeCurrent( fgDisplay.pDisplay.Display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                        EGL_NO_CONTEXT );
    fghDestroySurface( window );

    /* Destroy the context unless another window shares it */
    if( window->Window.Context != EGL_NO_CONTEXT )
    {
        for( iter = (SFG_Window *) fgStructure.Windows.First; iter && !used;
             iter = (SFG_Window *) iter->Node.Next )
            if( iter != window && iter->Window.Context == window->Window.Context )
                used = 1;
        if( !used )
            eglDestroyContext( fgDisplay.pDisplay.Display, window->Window.Context );
        window->Window.Context = EGL_NO_CONTEXT;
    }

    if( !window->Parent )
    {
        fghDeleteWimpWindow( window->Window.Handle );
        window->State.pWState.Open = 0;
    }
    else if( top && top != window )
        fghForceRedraw( top );
}

void fgPlatformSetWindow( SFG_Window *window )
{
    if( window && window != fgStructure.CurrentWindow )
        fghMakeCurrent( window );
}

void fgPlatformGlutSwapBuffers( SFG_PlatformDisplay *pDisplayPtr, SFG_Window* CurrentWindow )
{
    SFG_PlatformContext *pc = &CurrentWindow->Window.pContext;
    EGLint w = 0, h = 0;

    if( pc->Surface == EGL_NO_SURFACE )
        return;
    eglSwapBuffers( pDisplayPtr->Display, pc->Surface );
    CurrentWindow->State.pWState.Dirty = 0;

    /* A top-level surface takes the window's new size at a swap: the frame
       just shown was drawn at the old size, so draw another. */
    eglQuerySurface( pDisplayPtr->Display, pc->Surface, EGL_WIDTH, &w );
    eglQuerySurface( pDisplayPtr->Display, pc->Surface, EGL_HEIGHT, &h );
    if( w != pc->SurfaceWidth || h != pc->SurfaceHeight )
    {
        pc->SurfaceWidth = w;
        pc->SurfaceHeight = h;
        CurrentWindow->State.WorkMask |= GLUT_DISPLAY_WORK;
    }
}

/* Show what a window has drawn: single-buffered windows after the display
   callback (or after input callbacks drew into them). */
void fghRiscosPresent( SFG_Window *window )
{
    SFG_Window *current = fgStructure.CurrentWindow;
    if( !window || window->IsMenu || window->Window.pContext.Surface == EGL_NO_SURFACE )
        return;
    if( current != window )
        fgSetWindow( window );
    glFlush( );
    fgPlatformGlutSwapBuffers( &fgDisplay.pDisplay, window );
    if( current && current != window )
        fgSetWindow( current );
}

/* Called (by fg_main.c) after each display callback */
void fgPlatformDisplayDone( SFG_Window *window )
{
    if( !window->Window.DoubleBuffered )
        fghRiscosPresent( window );
}

void fgPlatformInitSwapCtl( void )
{
}

void fgPlatformSwapInterval( int n )
{
    eglSwapInterval( fgDisplay.pDisplay.Display, n );
}

int fgPlatformExtSupported( const char *ext )
{
    (void) ext;
    return 0;
}

void fgPlatformReshapeWindow( SFG_Window *window, int width, int height )
{
    if( window->Parent )
    {
        fghDestroySurface( window );
        fghOnReshapeNotify( window, width, height, GL_FALSE );
        if( window->State.Visible )
            fghCreateSurface( window );
        fghForceRedraw( fghRiscosTopWindow( window ) );
        return;
    }
    if( !window->State.pWState.Open )
    {
        window->State.Width = width;
        window->State.Height = height;
        return;
    }
    {
        int x, y, w, h;
        fghVisibleToGlut( window, &x, &y, &w, &h );
        fghOpenWimpWindow( window, x, y, width, height, -3 );
        fghRiscosWindowOpened( window );
    }
}

void fgPlatformPositionWindow( SFG_Window *window, int x, int y )
{
    if( window->Parent )
    {
        window->State.Xpos = x;
        window->State.Ypos = y;
        if( window->State.Visible )
        {
            fghDestroySurface( window );
            fghCreateSurface( window );
        }
        fghRecreateSubSurfaces( window );
        fghForceRedraw( fghRiscosTopWindow( window ) );
        fghOnPositionNotify( window, x, y, GL_TRUE );
        window->State.WorkMask |= GLUT_DISPLAY_WORK;
        return;
    }
    if( !window->State.pWState.Open )
    {
        window->State.Xpos = x;
        window->State.Ypos = y;
        return;
    }
    fghOpenWimpWindow( window, x, y, window->State.Width, window->State.Height, -3 );
    fghRiscosWindowOpened( window );
}

void fgPlatformPushWindow( SFG_Window *window )
{
    window = fghRiscosTopWindow( window );
    if( window->State.pWState.Open )
        fghOpenWimpWindow( window, window->State.Xpos, window->State.Ypos,
                           window->State.Width, window->State.Height, -2 );
}

void fgPlatformPopWindow( SFG_Window *window )
{
    window = fghRiscosTopWindow( window );
    if( window->State.pWState.Open )
        fghOpenWimpWindow( window, window->State.Xpos, window->State.Ypos,
                           window->State.Width, window->State.Height, -1 );
}

void fgPlatformShowWindow( SFG_Window *window )
{
    if( window->Parent )
    {
        fghCreateSurface( window );
    }
    else if( !window->State.pWState.Open )
    {
        fghOpenWimpWindow( window, window->State.Xpos, window->State.Ypos,
                           window->State.Width, window->State.Height, -1 );
        fghRiscosWindowOpened( window );
    }
    if( !window->State.Visible )
    {
        window->State.Visible = GL_TRUE;
        INVOKE_WCB( *window, WindowStatus, ( GLUT_FULLY_RETAINED ) );
    }
    window->State.WorkMask |= GLUT_DISPLAY_WORK;
}

void fgPlatformHideWindow( SFG_Window *window )
{
    _kernel_swi_regs r;

    if( window->IsMenu )
        return;
    if( window->Parent )
    {
        fghDestroySurface( window );
        fghForceRedraw( fghRiscosTopWindow( window ) );
    }
    else if( window->State.pWState.Open )
    {
        int block[1];
        block[0] = window->Window.Handle;
        r.r[1] = (int) block;
        _kernel_swi( Wimp_CloseWindow_, &r, &r );
        window->State.pWState.Open = 0;
    }
    if( window->State.Visible )
    {
        window->State.Visible = GL_FALSE;
        INVOKE_WCB( *window, WindowStatus, ( GLUT_HIDDEN ) );
    }
}

/* There is no iconising without a Pinboard protocol exchange: close the
   window, as glutHideWindow does. */
void fgPlatformIconifyWindow( SFG_Window *window )
{
    fgPlatformHideWindow( window );
}

void fgPlatformFullScreenToggle( SFG_Window *window )
{
    SFG_PlatformWindowState *ws = &window->State.pWState;

    if( window->Parent )
        return;
    if( !window->State.IsFullscreen )
    {
        ws->Normal[0] = window->State.Xpos;
        ws->Normal[1] = window->State.Ypos;
        ws->Normal[2] = window->State.Width;
        ws->Normal[3] = window->State.Height;
        window->State.IsFullscreen = GL_TRUE;
        fghReplaceWimpWindow( window, 1, 0, 0,
                              fgDisplay.ScreenWidth, fgDisplay.ScreenHeight );
    }
    else
    {
        window->State.IsFullscreen = GL_FALSE;
        fghReplaceWimpWindow( window, ( fgState.DisplayMode & GLUT_BORDERLESS ) != 0,
                              ws->Normal[0], ws->Normal[1], ws->Normal[2], ws->Normal[3] );
    }
}

void fgPlatformGlutSetWindowTitle( const char* title )
{
    SFG_Window *window = fghRiscosTopWindow( fgStructure.CurrentWindow );
    _kernel_swi_regs r;

    if( !window )
        return;
    snprintf( window->State.pWState.Title, sizeof window->State.pWState.Title,
              "%s", title ? title : "" );
    if( window->State.pWState.Open && !window->State.pWState.Borderless )
    {
        r.r[0] = window->Window.Handle;
        r.r[1] = TASK_WORD;
        r.r[2] = 3;                     /* redraw the title bar */
        _kernel_swi( Wimp_ForceRedraw_, &r, &r );
    }
}

void fgPlatformGlutSetIconTitle( const char* title )
{
    (void) title;
}


/* -- Pointer ------------------------------------------------------------ */

void fgPlatformSetCursor( SFG_Window *window, int cursorID )
{
    /* RISC OS has one pointer shape; GLUT_CURSOR_NONE hides it while it is
       over the window. */
    window->State.Cursor = cursorID;
    fghRiscosUpdatePointerShape( );
}

void fgPlatformWarpPointer( int x, int y )
{
    SFG_Window *window = fgStructure.CurrentWindow, *top;
    unsigned char block[5];
    int ox, oy, sx, sy;
    _kernel_swi_regs r;

    top = fghRiscosTopWindow( window );
    if( !top || !top->State.pWState.Open )
        return;
    fghRiscosClientOrigin( window, &ox, &oy );
    sx = top->State.pWState.Visible[0] + ( ( ox + x ) << XEIG );
    sy = top->State.pWState.Visible[3] - ( ( oy + y ) << YEIG ) - ( 1 << YEIG );
    block[1] = sx & 255; block[2] = ( sx >> 8 ) & 255;
    block[3] = sy & 255; block[4] = ( sy >> 8 ) & 255;
    block[0] = 3;                       /* set mouse position */
    r.r[0] = 21;
    r.r[1] = (int) block;
    _kernel_swi( OS_Word_, &r, &r );
    block[0] = 5;                       /* set pointer position */
    _kernel_swi( OS_Word_, &r, &r );
}

/* Pointer position in pixels: relative to window's client area, or on the
   screen from its top left. */
void fghPlatformGetCursorPos( const SFG_Window *window, GLboolean client, SFG_XYUse *mouse_pos )
{
    int block[5], ox, oy;
    _kernel_swi_regs r;
    SFG_Window *top;

    mouse_pos->X = mouse_pos->Y = 0;
    mouse_pos->Use = GL_FALSE;
    r.r[1] = (int) block;
    if( _kernel_swi( Wimp_GetPointerInfo_, &r, &r ) != NULL )
        return;
    mouse_pos->Use = GL_TRUE;
    top = fghRiscosTopWindow( (SFG_Window *) window );
    if( client && top && top->State.pWState.Open )
    {
        fghRiscosClientOrigin( (SFG_Window *) window, &ox, &oy );
        mouse_pos->X = ( ( block[0] - top->State.pWState.Visible[0] ) >> XEIG ) - ox;
        mouse_pos->Y = ( ( top->State.pWState.Visible[3] - 1 - block[1] ) >> YEIG ) - oy;
    }
    else
    {
        mouse_pos->X = block[0] >> XEIG;
        mouse_pos->Y = ( SCREEN_TOP - 1 - block[1] ) >> YEIG;
    }
}

/* Title bar height and border width, for glutGet */
int fghRiscosFrameSize( SFG_Window *window, int header )
{
    int block[5];
    _kernel_swi_regs r;

    window = fghRiscosTopWindow( window );
    if( !window || window->State.pWState.Borderless || window->State.IsFullscreen )
        return 0;
    block[0] = window->Window.Handle;
    r.r[1] = (int) block;
    if( window->State.pWState.Open &&
        _kernel_swi( Wimp_GetWindowOutline_, &r, &r ) == NULL )
    {
        if( header )
            return ( block[4] - window->State.pWState.Visible[3] ) >> YEIG;
        return ( window->State.pWState.Visible[0] - block[1] ) >> XEIG;
    }
    return header ? 40 >> YEIG : 0;     /* a typical RISC OS 5 title bar */
}
