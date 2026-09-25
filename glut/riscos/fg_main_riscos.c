/*
 * fg_main_riscos.c
 *
 * The RISC OS back end of freeglut: the Wimp_Poll loop, the keyboard and
 * the mouse.
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
 * Events
 *
 * glutMainLoopEvent calls fgPlatformProcessSingleEvent, which polls the
 * Wimp (with null events) until nothing is waiting: every pass of the loop
 * lets the other tasks run. When the program has nothing to do,
 * fgPlatformSleepForEvents waits in Wimp_Poll (or Wimp_PollIdle until the
 * next timer), so an idle GLUT program costs no CPU time.
 *
 * The Wimp reports key presses and mouse clicks, not releases or movement.
 * Those come from reading the pointer (Wimp_GetPointerInfo), the scroll
 * wheel (OS_Pointer 2) and the keyboard (OS_Byte 121) after each poll;
 * while a button or key is held, or the pointer is over a window that
 * wants motion, the waits are kept short (20 ms) so they're seen promptly.
 *
 * Mouse buttons: Select is GLUT_LEFT_BUTTON, Menu GLUT_MIDDLE_BUTTON and
 * Adjust GLUT_RIGHT_BUTTON (their places on the mouse, as in SDL). Menu
 * also opens a GLUT menu attached to any button, since that's what Menu
 * does on RISC OS.
 */

#include <limits.h>
#include <kernel.h>
#include <swis.h>
#include <GL/freeglut.h>
#include "../fg_internal.h"

EGLAPI EGLBoolean EGLAPIENTRY eglRedrawWindowRISCOS( EGLDisplay dpy, int *block );

#define Wimp_Poll_              0x400C7
#define Wimp_PollIdle_          0x400E1
#define Wimp_OpenWindow_        0x400C5
#define Wimp_RedrawWindow_      0x400C8
#define Wimp_GetRectangle_      0x400CA
#define Wimp_ProcessKey_        0x400DC
#define Wimp_GetPointerInfo_    0x400CF
#define OS_ReadMonotonicTime_   0x42
#define OS_Pointer_             0x64

#define SELECT  4
#define MENU    2
#define ADJUST  1

#define FAST_POLL_MS 20                 /* wait while watching the pointer/keys */

extern void fghOnReshapeNotify( SFG_Window *window, int width, int height,
                                GLboolean forceNotify );
extern void fghOnPositionNotify( SFG_Window *window, int x, int y,
                                 GLboolean forceNotify );
extern void fghRiscosWindowOpened( SFG_Window *window );
extern void fgPlatformFullScreenToggle( SFG_Window *window );
extern void fgPlatformPositionWindow( SFG_Window *window, int x, int y );
extern void fgPlatformReshapeWindow( SFG_Window *window, int width, int height );
extern void fgPlatformPushWindow( SFG_Window *window );
extern void fgPlatformPopWindow( SFG_Window *window );
extern void fgPlatformHideWindow( SFG_Window *window );
extern void fgPlatformShowWindow( SFG_Window *window );
extern void fgPlatformIconifyWindow( SFG_Window *window );
extern void fghRiscosModeChanged( void );

/* An event read while waiting, handled at the next poll */
static int fghPendingReason = -1;
static int fghPendingBlock[ 64 ];

static int fghFocusID;                  /* top-level window with the caret */
static int fghUnderID;                  /* window under the pointer */
static int fghCaptureID;                /* window the held buttons were pressed in */
static int fghHeldButtons;              /* GLUT buttons held (1 << button) */
static int fghLastX = INT_MIN, fghLastY;/* pointer, OS units */
static int fghWheelValid, fghWheelY;
static int fghPointerOff;               /* pointer hidden (GLUT_CURSOR_NONE) */
static int fghModifierKeys;             /* internal keys 3-8 held (bits) */

/* Keys held down, so their release can be reported */
#define MAX_HELD_KEYS 8
static struct {
    int internal;                       /* internal key number */
    int key;                            /* the character or GLUT_KEY_* */
    int special;
    int window;                         /* window ID */
} fghHeldKeys[ MAX_HELD_KEYS ];
static int fghNumHeldKeys;

static const int fghButtonBits[ 3 ] = { SELECT, MENU, ADJUST };


fg_time_t fgPlatformSystemTime( void )
{
    _kernel_swi_regs r;
    _kernel_swi( OS_ReadMonotonicTime_, &r, &r );
    return (fg_time_t) (unsigned int) r.r[0] * 10u;
}


/* -- Helpers -------------------------------------------------------------- */

static int fghKeyDown( int internal )
{
    return ( _kernel_osbyte( 121, internal ^ 0x80, 0 ) & 0xFF ) == 0xFF;
}

static int fghModifiers( void )
{
    int m = 0;
    if( fghKeyDown( 0 ) ) m |= GLUT_ACTIVE_SHIFT;
    if( fghKeyDown( 1 ) ) m |= GLUT_ACTIVE_CTRL;
    if( fghKeyDown( 2 ) ) m |= GLUT_ACTIVE_ALT;
    return m;
}

/* The top-level window with this Wimp handle */
static SFG_Window *fghWindowByHandle( int handle )
{
    SFG_Window *w;
    for( w = (SFG_Window *) fgStructure.Windows.First; w;
         w = (SFG_Window *) w->Node.Next )
        if( !w->IsMenu && w->Window.Handle == handle )
            return w;
    return NULL;
}

/* The (sub)window at pixel x, y of a top-level window's client area. */
static SFG_Window *fghWindowAt( SFG_Window *window, int x, int y )
{
    SFG_Window *child, *hit;
    for( ;; )
    {
        hit = NULL;
        for( child = (SFG_Window *) window->Children.First; child;
             child = (SFG_Window *) child->Node.Next )
            if( child->State.Visible &&
                x >= child->State.Xpos && x < child->State.Xpos + child->State.Width &&
                y >= child->State.Ypos && y < child->State.Ypos + child->State.Height )
                hit = child;            /* the last one is on top */
        if( !hit )
            return window;
        x -= hit->State.Xpos;
        y -= hit->State.Ypos;
        window = hit;
    }
}

/* Screen position (OS units) to a window's client pixels */
static void fghClientXY( SFG_Window *window, int sx, int sy, int *x, int *y )
{
    SFG_Window *top = fghRiscosTopWindow( window );
    int ox, oy;
    fghRiscosClientOrigin( window, &ox, &oy );
    *x = ( ( sx - top->State.pWState.Visible[0] ) >> fgDisplay.pDisplay.XEig ) - ox;
    *y = ( ( top->State.pWState.Visible[3] - 1 - sy ) >> fgDisplay.pDisplay.YEig ) - oy;
}

/* The window under the pointer at sx, sy (OS units) over Wimp window handle */
static SFG_Window *fghPointerWindow( int handle, int sx, int sy )
{
    SFG_Window *top = fghWindowByHandle( handle );
    const int *v;
    int x, y;
    if( !top || !top->State.pWState.Open )
        return NULL;
    v = top->State.pWState.Visible;
    if( sx < v[0] || sx >= v[2] || sy < v[1] || sy >= v[3] )
        return NULL;
    fghClientXY( top, sx, sy, &x, &y );
    return fghWindowAt( top, x, y );
}

/* After a callback: a single-buffered window may have been drawn into */
static void fghTouched( SFG_Window *window )
{
    if( window && fgWindowByID( window->ID ) == window && !window->Window.DoubleBuffered )
        window->State.pWState.Dirty = 1;
}

static void fghPresentDirty( SFG_Window *window )
{
    SFG_Window *child;
    if( window->State.pWState.Dirty && window->State.Visible )
        fghRiscosPresent( window );
    window->State.pWState.Dirty = 0;
    for( child = (SFG_Window *) window->Children.First; child;
         child = (SFG_Window *) child->Node.Next )
        fghPresentDirty( child );
}

static void fghMarkTree( SFG_Window *window )
{
    SFG_Window *child;
    if( !window->IsMenu && !window->Window.DoubleBuffered )
        window->State.pWState.Dirty = 1;
    for( child = (SFG_Window *) window->Children.First; child;
         child = (SFG_Window *) child->Node.Next )
        fghMarkTree( child );
}

static void fghMarkSingleBuffered( void )
{
    SFG_Window *window;
    for( window = (SFG_Window *) fgStructure.Windows.First; window;
         window = (SFG_Window *) window->Node.Next )
        fghMarkTree( window );
}

int fghRiscosPointerHidden( void )
{
    return fghPointerOff;
}

/* Hide the pointer while it is over a window with GLUT_CURSOR_NONE. */
void fghRiscosUpdatePointerShape( void )
{
    SFG_Window *w = fgWindowByID( fghUnderID );
    int off = w && w->State.Cursor == GLUT_CURSOR_NONE;
    if( off != fghPointerOff )
    {
        _kernel_osbyte( 106, off ? 0 : 1, 0 );
        fghPointerOff = off;
    }
}


/* -- Mouse ---------------------------------------------------------------- */

static void fghMouseCallback( SFG_Window *window, int button, int state, int x, int y )
{
    if( !FETCH_WCB( *window, Mouse ) )
        return;
    fgState.Modifiers = fghModifiers( );
    INVOKE_WCB( *window, Mouse, ( button, state, x, y ) );
    fgState.Modifiers = INVALID_MODIFIERS;
    fghTouched( window );
}

/* Mouse_Click: a button went down */
static void fghMouseClick( const int *block )
{
    SFG_Window *window, *top = fghWindowByHandle( block[3] );
    int button, x, y;

    if( !top )
        return;
    if( fghFocusID != top->ID )
        fghRiscosTakeFocus( top );
    window = fghPointerWindow( block[3], block[0], block[1] );
    if( !window )
        return;
    fghLastX = block[0];                /* no motion reported for the click itself */
    fghLastY = block[1];
    fghClientXY( window, block[0], block[1], &x, &y );
    window->State.MouseX = x;
    window->State.MouseY = y;

    for( button = 0; button < 3; button++ )
    {
        SFG_Menu *menu;
        if( !( block[2] & fghButtonBits[ button ] ) ||
            ( fghHeldButtons & ( 1 << button ) ) )
            continue;

        /* A menu attached to this button, or (for Menu) to any button */
        menu = window->Menu[ button ];
        if( !menu && fghButtonBits[ button ] == MENU )
            menu = window->Menu[ GLUT_RIGHT_BUTTON ] ? window->Menu[ GLUT_RIGHT_BUTTON ]
                                                     : window->Menu[ GLUT_LEFT_BUTTON ];
        if( menu )
        {
            fghRiscosOpenMenu( window, menu, block[0], block[1] );
            continue;
        }

        fghHeldButtons |= 1 << button;
        fghCaptureID = window->ID;
        fghMouseCallback( window, button, GLUT_DOWN, x, y );
        window = fgWindowByID( fghCaptureID );
        if( !window )
            return;
    }
}

static void fghWheel( SFG_Window *window, int notches, int x, int y )
{
    int dir = notches > 0 ? 1 : -1, i;
    if( notches < 0 )
        notches = -notches;
    if( notches > 10 )
        notches = 10;
    for( i = 0; i < notches && fgWindowByID( window->ID ) == window; i++ )
    {
        fgState.Modifiers = fghModifiers( );
        if( FETCH_WCB( *window, MouseWheel ) )
            INVOKE_WCB( *window, MouseWheel, ( 0, dir, x, y ) );
        else if( FETCH_WCB( *window, Mouse ) )
        {
            /* as buttons 3 (up) and 4 (down), like X11 GLUT */
            int b = dir > 0 ? 3 : 4;
            INVOKE_WCB( *window, Mouse, ( b, GLUT_DOWN, x, y ) );
            if( fgWindowByID( window->ID ) == window )
                INVOKE_WCB( *window, Mouse, ( b, GLUT_UP, x, y ) );
        }
        fgState.Modifiers = INVALID_MODIFIERS;
        fghTouched( window );
    }
}


/* -- Keyboard ------------------------------------------------------------- */

/* Wimp key code to a character (return 1) or GLUT_KEY_* (return 2); 0 if
   GLUT has no such key. */
static int fghTranslateKey( int code, int *key )
{
    int base;

    if( code == 0x1E )                  /* Home */
    {
        *key = GLUT_KEY_HOME;
        return 2;
    }
    if( code < 0x100 )
    {
        *key = code;
        return 1;
    }
    /* Function and cursor keys: Shift adds 0x10, Ctrl 0x20 */
    base = code & ~0x30;
    switch( base )
    {
    case 0x181: case 0x182: case 0x183: case 0x184: case 0x185:
    case 0x186: case 0x187: case 0x188: case 0x189:
        *key = GLUT_KEY_F1 + ( base - 0x181 );
        return 2;
    case 0x1CA: *key = GLUT_KEY_F10; return 2;
    case 0x1CB: *key = GLUT_KEY_F11; return 2;
    case 0x1CC: *key = GLUT_KEY_F12; return 2;
    case 0x1CD: *key = GLUT_KEY_INSERT; return 2;
    case 0x18A: *key = 9; return 1;     /* Tab */
    case 0x18B: *key = GLUT_KEY_END; return 2;      /* Copy / End */
    case 0x18C: *key = GLUT_KEY_LEFT; return 2;
    case 0x18D: *key = GLUT_KEY_RIGHT; return 2;
    case 0x18E:
    case 0x18F:
        /* Page Down / Page Up are Shift+Down / Shift+Up: tell them apart
           by whether Shift is held */
        if( ( code & 0x10 ) && !fghKeyDown( 0 ) )
            *key = base == 0x18E ? GLUT_KEY_PAGE_DOWN : GLUT_KEY_PAGE_UP;
        else
            *key = base == 0x18E ? GLUT_KEY_DOWN : GLUT_KEY_UP;
        return 2;
    }
    return 0;
}

static int fghHeldKeyIndex( int internal )
{
    int i;
    for( i = 0; i < fghNumHeldKeys; i++ )
        if( fghHeldKeys[ i ].internal == internal )
            return i;
    return -1;
}

/* The internal key number of a key pressed and not yet recorded, or -1 */
static int fghNewlyPressedKey( void )
{
    int k = 12;                         /* past Shift, Ctrl, Alt and the mouse */
    while( k < 0x80 )
    {
        int found = _kernel_osbyte( 121, k, 0 ) & 0xFF;
        if( found < 12 || found >= 0x80 )
            return -1;
        if( fghHeldKeyIndex( found ) < 0 )
            return found;
        k = found + 1;
    }
    return -1;
}

static void fghKeyCallback( SFG_Window *window, int kind, int key, int down )
{
    int x = window->State.MouseX, y = window->State.MouseY;
    fgState.Modifiers = fghModifiers( );
    if( kind == 1 )
    {
        if( down )
            INVOKE_WCB( *window, Keyboard, ( (unsigned char) key, x, y ) );
        else
            INVOKE_WCB( *window, KeyboardUp, ( (unsigned char) key, x, y ) );
    }
    else
    {
        if( down )
            INVOKE_WCB( *window, Special, ( key, x, y ) );
        else
            INVOKE_WCB( *window, SpecialUp, ( key, x, y ) );
    }
    fgState.Modifiers = INVALID_MODIFIERS;
    fghTouched( window );
}

/* The window keys go to: the one under the pointer if it's inside the
   window with the caret, else that window. */
static SFG_Window *fghKeyWindow( int handle )
{
    SFG_Window *top = fghWindowByHandle( handle ), *under = fgWindowByID( fghUnderID );
    if( !top )
        return NULL;
    if( under && fghRiscosTopWindow( under ) == top )
        return under;
    return top;
}

static void fghKeyPressed( const int *block )
{
    SFG_Window *window = fghKeyWindow( block[0] );
    int code = block[6], key, kind, internal, i, used;
    _kernel_swi_regs r;

    kind = fghTranslateKey( code, &key );
    used = window && kind &&
           ( kind == 1 ? ( FETCH_WCB( *window, Keyboard ) || FETCH_WCB( *window, KeyboardUp ) )
                       : ( FETCH_WCB( *window, Special ) || FETCH_WCB( *window, SpecialUp ) ) );
    if( !used )
    {
        r.r[0] = code;
        _kernel_swi( Wimp_ProcessKey_, &r, &r );
        return;
    }

    internal = fghNewlyPressedKey( );
    if( internal < 0 )
    {
        /* No new key down: auto-repeat of a held key, or a key that was
           already released (or typed by software) */
        for( i = 0; i < fghNumHeldKeys; i++ )
            if( fghHeldKeys[ i ].key == key && fghHeldKeys[ i ].special == ( kind == 2 ) )
                break;
        if( i < fghNumHeldKeys )
        {
            if( fgState.KeyRepeat == GLUT_KEY_REPEAT_OFF || window->State.IgnoreKeyRepeat )
                return;
            fghKeyCallback( window, kind, key, 1 );
            return;
        }
        fghKeyCallback( window, kind, key, 1 );
        if( fgWindowByID( window->ID ) == window )
            fghKeyCallback( window, kind, key, 0 );
        return;
    }

    if( fghNumHeldKeys < MAX_HELD_KEYS )
    {
        fghHeldKeys[ fghNumHeldKeys ].internal = internal;
        fghHeldKeys[ fghNumHeldKeys ].key = key;
        fghHeldKeys[ fghNumHeldKeys ].special = ( kind == 2 );
        fghHeldKeys[ fghNumHeldKeys ].window = window->ID;
        fghNumHeldKeys++;
    }
    fghKeyCallback( window, kind, key, 1 );
}

/* Report released keys, and Shift/Ctrl/Alt as GLUT_KEY_SHIFT_L etc. */
static void fghPollKeys( void )
{
    static const int modifier_keys[ 6 ] = {
        GLUT_KEY_SHIFT_L, GLUT_KEY_CTRL_L, GLUT_KEY_ALT_L,
        GLUT_KEY_SHIFT_R, GLUT_KEY_CTRL_R, GLUT_KEY_ALT_R
    };
    SFG_Window *window;
    int i, now = 0;

    for( i = 0; i < fghNumHeldKeys; )
    {
        if( fghKeyDown( fghHeldKeys[ i ].internal ) )
        {
            i++;
            continue;
        }
        window = fgWindowByID( fghHeldKeys[ i ].window );
        {
            int key = fghHeldKeys[ i ].key, kind = fghHeldKeys[ i ].special ? 2 : 1;
            fghHeldKeys[ i ] = fghHeldKeys[ --fghNumHeldKeys ];
            if( window )
                fghKeyCallback( window, kind, key, 0 );
        }
    }

    window = fgWindowByID( fghFocusID );
    if( !window || ( !FETCH_WCB( *window, Special ) && !FETCH_WCB( *window, SpecialUp ) ) )
    {
        fghModifierKeys = 0;
        return;
    }
    window = fghKeyWindow( window->Window.Handle );
    for( i = 0; i < 6; i++ )
        if( fghKeyDown( 3 + i ) )
            now |= 1 << i;
    for( i = 0; i < 6 && window; i++ )
        if( ( now ^ fghModifierKeys ) & ( 1 << i ) )
        {
            fghKeyCallback( window, 2, modifier_keys[ i ], ( now >> i ) & 1 );
            if( fgWindowByID( window->ID ) != window )
                window = NULL;
        }
    fghModifierKeys = now;
}


/* -- Pointer polling ------------------------------------------------------ */

static void fghPollPointer( void )
{
    int block[5], x, y, moved, button, dy;
    SFG_Window *under, *old, *capture;
    _kernel_swi_regs r;

    r.r[1] = (int) block;
    if( _kernel_swi( Wimp_GetPointerInfo_, &r, &r ) != NULL )
        return;
    under = fghPointerWindow( block[3], block[0], block[1] );

    /* Entry callbacks */
    if( ( under ? under->ID : 0 ) != fghUnderID )
    {
        old = fgWindowByID( fghUnderID );
        fghUnderID = under ? under->ID : 0;
        if( old )
            INVOKE_WCB( *old, Entry, ( GLUT_LEFT ) );
        if( under && fgWindowByID( fghUnderID ) == under )
            INVOKE_WCB( *under, Entry, ( GLUT_ENTERED ) );
        under = fgWindowByID( fghUnderID );
        fghRiscosUpdatePointerShape( );
    }

    /* Movement: to the window holding the buttons, else the one under the
       pointer */
    moved = block[0] != fghLastX || block[1] != fghLastY;
    fghLastX = block[0];
    fghLastY = block[1];
    capture = fghHeldButtons ? fgWindowByID( fghCaptureID ) : NULL;
    if( moved )
    {
        SFG_Window *to = capture ? capture : under;
        if( to )
        {
            fghClientXY( to, block[0], block[1], &x, &y );
            to->State.MouseX = x;
            to->State.MouseY = y;
            fgState.Modifiers = fghModifiers( );
            if( capture )
                INVOKE_WCB( *to, Motion, ( x, y ) );
            else
                INVOKE_WCB( *to, Passive, ( x, y ) );
            fgState.Modifiers = INVALID_MODIFIERS;
            fghTouched( to );
        }
    }

    /* Released buttons */
    for( button = 0; button < 3; button++ )
    {
        if( !( fghHeldButtons & ( 1 << button ) ) || ( block[2] & fghButtonBits[ button ] ) )
            continue;
        fghHeldButtons &= ~( 1 << button );
        capture = fgWindowByID( fghCaptureID );
        if( capture )
        {
            fghClientXY( capture, block[0], block[1], &x, &y );
            fghMouseCallback( capture, button, GLUT_UP, x, y );
        }
    }
    if( !fghHeldButtons )
        fghCaptureID = 0;

    /* The scroll wheel: OS_Pointer 2 gives its running count, +ve Y up */
    r.r[0] = 2;
    if( _kernel_swi( OS_Pointer_, &r, &r ) == NULL )
    {
        dy = r.r[1] - fghWheelY;
        fghWheelY = r.r[1];
        if( !fghWheelValid )
            fghWheelValid = 1;
        else if( dy && dy >= -64 && dy <= 64 )
        {
            under = fgWindowByID( fghUnderID );
            if( under )
            {
                fghClientXY( under, block[0], block[1], &x, &y );
                fghWheel( under, dy, x, y );
            }
        }
    }
}

/* Is anything being watched that needs short waits? */
static int fghWatching( void )
{
    SFG_Window *under;
    if( fghHeldButtons || fghNumHeldKeys )
        return 1;
    under = fgWindowByID( fghUnderID );
    return under && ( FETCH_WCB( *under, Passive ) || FETCH_WCB( *under, MouseWheel ) ||
                      FETCH_WCB( *under, Mouse ) || FETCH_WCB( *under, Entry ) );
}


/* -- Wimp events ---------------------------------------------------------- */

static void fghCloseRequest( SFG_Window *window )
{
    if( window == fgStructure.GameModeWindow )
        glutLeaveGameMode( );           /* destroys it */
    else
        fgDestroyWindow( window );
    if( fgState.ActionOnWindowClose == GLUT_ACTION_EXIT )
    {
        fgDeinitialize( );
        exit( 0 );
    }
    else if( fgState.ActionOnWindowClose == GLUT_ACTION_GLUTMAINLOOP_RETURNS )
        fgState.ExecState = GLUT_EXEC_STATE_STOP;
}

static void fghHandleEvent( int reason, int *block )
{
    SFG_Window *window;
    _kernel_swi_regs r;

    switch( reason )
    {
    case 1:                             /* Redraw_Window_Request */
        if( !eglRedrawWindowRISCOS( fgDisplay.pDisplay.Display, block ) )
        {
            r.r[1] = (int) block;
            _kernel_swi( Wimp_RedrawWindow_, &r, &r );
            while( r.r[0] )
            {
                r.r[1] = (int) block;
                _kernel_swi( Wimp_GetRectangle_, &r, &r );
            }
        }
        break;

    case 2:                             /* Open_Window_Request */
        r.r[1] = (int) block;
        _kernel_swi( Wimp_OpenWindow_, &r, &r );
        window = fghWindowByHandle( block[0] );
        if( window )
        {
            window->State.pWState.Open = 1;
            fghRiscosWindowOpened( window );
        }
        break;

    case 3:                             /* Close_Window_Request */
        window = fghWindowByHandle( block[0] );
        if( window )
            fghCloseRequest( window );
        break;

    case 4:                             /* Pointer_Leaving_Window */
    case 5:                             /* Pointer_Entering_Window */
        break;                          /* seen by fghPollPointer */

    case 6:                             /* Mouse_Click */
        fghMouseClick( block );
        break;

    case 8:                             /* Key_Pressed */
        fghKeyPressed( block );
        break;

    case 9:                             /* Menu_Selection */
        fghRiscosMenuSelection( block );
        break;

    case 11:                            /* Lose_Caret */
        window = fghWindowByHandle( block[0] );
        if( window && window->ID == fghFocusID )
            fghFocusID = 0;
        break;

    case 12:                            /* Gain_Caret */
        window = fghWindowByHandle( block[0] );
        fghFocusID = window ? window->ID : 0;
        break;

    case 17:                            /* User_Message */
    case 18:                            /* User_Message_Recorded */
        switch( block[4] )
        {
        case 0:                         /* Message_Quit */
            fgDeinitialize( );
            exit( 0 );
        case 0x400C1:                   /* Message_ModeChange */
            fghRiscosModeChanged( );
            break;
        case 0x400C9:                   /* Message_MenusDeleted */
            fghRiscosMenusDeleted( );
            break;
        }
        break;
    }
}

void fgPlatformProcessSingleEvent( void )
{
    int block[ 64 ], reason, i;
    SFG_Window *window;
    _kernel_swi_regs r;

    FREEGLUT_EXIT_IF_NOT_INITIALISED ( "glutMainLoopEvent" );

    /* Single-buffered programs may draw from idle or timer callbacks (and
       glFlush): show their windows every 20 ms while those run. */
    if( fgState.IdleCallback || fgState.Timers.First )
    {
        static fg_time_t last;
        fg_time_t now = fgPlatformSystemTime( );
        if( now - last >= 20 )
        {
            last = now;
            fghMarkSingleBuffered( );
        }
    }

    if( fghPendingReason >= 0 )
    {
        reason = fghPendingReason;
        fghPendingReason = -1;
        memcpy( block, fghPendingBlock, sizeof block );
        fghHandleEvent( reason, block );
    }

    /* Everything waiting, up to the first null event */
    for( i = 0; i < 64; i++ )
    {
        r.r[0] = 0;
        r.r[1] = (int) block;
        if( _kernel_swi( Wimp_Poll_, &r, &r ) != NULL )
            break;
        if( r.r[0] == 0 )
            break;
        fghHandleEvent( r.r[0], block );
        if( !fgStructure.Windows.First || fgState.ExecState != GLUT_EXEC_STATE_RUNNING )
            return;
    }

    fghPollPointer( );
    fghPollKeys( );

    for( window = (SFG_Window *) fgStructure.Windows.First; window;
         window = (SFG_Window *) window->Node.Next )
        if( !window->IsMenu )
            fghPresentDirty( window );
}

void fgPlatformSleepForEvents( fg_time_t msec )
{
    int mask = 0;
    _kernel_swi_regs r;

    if( fghPendingReason >= 0 )
        return;
    if( fghWatching( ) && msec > FAST_POLL_MS )
        msec = FAST_POLL_MS;

    r.r[1] = (int) fghPendingBlock;
    if( msec >= INT_MAX / 2 )
    {
        mask = 1;                       /* no null events: wait for an event */
        r.r[0] = mask;
        if( _kernel_swi( Wimp_Poll_, &r, &r ) != NULL )
            return;
    }
    else
    {
        _kernel_swi_regs t;
        _kernel_swi( OS_ReadMonotonicTime_, &t, &t );
        r.r[0] = mask;
        r.r[2] = t.r[0] + (int) ( ( msec + 9 ) / 10 );
        if( _kernel_swi( Wimp_PollIdle_, &r, &r ) != NULL )
            return;
    }
    if( r.r[0] != 0 )
        fghPendingReason = r.r[0];
}

void fgPlatformMainLoopPreliminaryWork( void )
{
}

void fgPlatformInitWork( SFG_Window* window )
{
    /* The first display is next: tell the program where the window is and
       how big (GLUT calls these before the first display callback). */
    fghOnPositionNotify( window, window->State.Xpos, window->State.Ypos, GL_TRUE );
    fghOnReshapeNotify( window, window->State.Width, window->State.Height, GL_TRUE );
    if( window->State.Visible )
        INVOKE_WCB( *window, WindowStatus, ( GLUT_FULLY_RETAINED ) );
}

void fgPlatformPosResZordWork( SFG_Window* window, unsigned int workMask )
{
    if( workMask & GLUT_FULL_SCREEN_WORK )
        fgPlatformFullScreenToggle( window );
    if( workMask & GLUT_POSITION_WORK )
        fgPlatformPositionWindow( window, window->State.DesiredXpos, window->State.DesiredYpos );
    if( workMask & GLUT_SIZE_WORK )
        fgPlatformReshapeWindow( window, window->State.DesiredWidth, window->State.DesiredHeight );
    if( workMask & GLUT_ZORDER_WORK )
    {
        if( window->State.DesiredZOrder < 0 )
            fgPlatformPushWindow( window );
        else
            fgPlatformPopWindow( window );
    }
}

void fgPlatformVisibilityWork( SFG_Window* window )
{
    SFG_Window *win = window;
    switch( window->State.DesiredVisibility )
    {
    case DesireHiddenState:
        fgPlatformHideWindow( window );
        break;
    case DesireIconicState:
        while( win->Parent )
            win = win->Parent;
        fgPlatformIconifyWindow( win );
        break;
    case DesireNormalState:
        fgPlatformShowWindow( window );
        break;
    }
}
