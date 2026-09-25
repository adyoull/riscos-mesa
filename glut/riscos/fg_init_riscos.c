/*
 * fg_init_riscos.c
 *
 * The RISC OS back end of freeglut: starting up and closing down (the Wimp
 * task and EGL), game mode, and the devices RISC OS doesn't have.
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

#include <errno.h>
#include <kernel.h>
#include <swis.h>
#include <unixlib/local.h>
#include <GL/freeglut.h>
#include "../fg_internal.h"

#ifndef Wimp_Initialise
#define Wimp_Initialise     0x400C0
#endif
#ifndef Wimp_CloseDown
#define Wimp_CloseDown      0x400DD
#endif
#ifndef OS_FSControl
#define OS_FSControl        0x29
#endif

#define TASK_WORD 0x4B534154            /* "TASK" */

extern char *program_invocation_name;
extern char *program_invocation_short_name;

/* The messages we want besides Message_Quit */
static const int fghMessages[] = {
    0x400C1,                            /* Message_ModeChange */
    0x400C9,                            /* Message_MenusDeleted */
    0
};

/*
 * The task name: the program's application directory without the "!"
 * (…!Gears.!RunImage -> "Gears"), else the program's own name.
 * <AppName>$TaskName isn't needed: GLUT_APP_NAME-style overrides come from
 * the FreeGLUT$TaskName system variable if it is set.
 */
static void fghFindTaskName( char *name, size_t size )
{
    char ro[256], canon[256];
    const char *path = program_invocation_name, *override;
    char *leaf = NULL, *end;
    _kernel_swi_regs r;

    name[0] = 0;
    override = getenv( "FreeGLUT$TaskName" );
    if( override && *override )
    {
        snprintf( name, size, "%s", override );
        return;
    }
    if( path && *path )
    {
        /* argv[0] can be a Unix or a RISC OS path: make it a RISC OS one */
        if( strchr( path, '/' ) &&
            __riscosify_std( path, 0, ro, sizeof ro, NULL ) )
            path = ro;
        r.r[0] = 37;                    /* canonicalise path */
        r.r[1] = (int) path;
        r.r[2] = (int) canon;
        r.r[3] = 0;
        r.r[4] = 0;
        r.r[5] = sizeof canon;
        if( _kernel_swi( OS_FSControl, &r, &r ) == NULL )
        {
            end = strrchr( canon, '.' );
            if( end )
            {
                *end = 0;
                leaf = strrchr( canon, '.' );
                leaf = leaf ? leaf + 1 : canon;
                if( *leaf != '!' || !leaf[1] )
                    leaf = NULL;
            }
        }
    }
    if( leaf )
        snprintf( name, size, "%s", leaf + 1 );
    else if( program_invocation_short_name && *program_invocation_short_name )
        snprintf( name, size, "%s", program_invocation_short_name );
    if( !name[0] )
        snprintf( name, size, "freeglut" );
}

static void fghInitialiseEGL( void )
{
    SFG_PlatformDisplay *d = &fgDisplay.pDisplay;

    d->Display = eglGetDisplay( EGL_DEFAULT_DISPLAY );
    if( d->Display == EGL_NO_DISPLAY )
        fgError( "No EGL display" );
    if( eglInitialize( d->Display, &d->MajorVersion, &d->MinorVersion ) != EGL_TRUE )
        fgError( "eglInitialize: error %x", eglGetError() );
#ifdef FREEGLUT_GLES
    eglBindAPI( EGL_OPENGL_ES_API );
#else
    eglBindAPI( EGL_OPENGL_API );
#endif
}

void fgPlatformInitialize( const char* displayName )
{
    SFG_PlatformDisplay *d = &fgDisplay.pDisplay;
    _kernel_swi_regs r;

    (void) displayName;

    fghFindTaskName( d->TaskName, sizeof d->TaskName );
    r.r[0] = 380;                       /* RISC OS 3.8+ (nested windows) */
    r.r[1] = TASK_WORD;
    r.r[2] = (int) d->TaskName;
    r.r[3] = (int) fghMessages;
    if( _kernel_swi( Wimp_Initialise, &r, &r ) != NULL )
    {
        r.r[0] = 310;                   /* older Wimps */
        r.r[1] = TASK_WORD;
        r.r[2] = (int) d->TaskName;
        r.r[3] = (int) fghMessages;
        if( _kernel_swi( Wimp_Initialise, &r, &r ) != NULL )
            fgError( "Wimp_Initialise failed" );
    }
    d->Task = r.r[1];

    fghRiscosReadScreen( );
    fghInitialiseEGL( );

    /* Get start time */
    fgState.Time = fgSystemTime();

    fgState.Initialised = GL_TRUE;

    atexit( fgDeinitialize );

    /* InputDevice uses GlutTimerFunc(), so fgState.Initialised must be TRUE */
    fgInitialiseInputDevices();
}

void fgPlatformCloseDisplay( void )
{
    SFG_PlatformDisplay *d = &fgDisplay.pDisplay;
    _kernel_swi_regs r;

    if( d->Display != EGL_NO_DISPLAY )
    {
        eglMakeCurrent( d->Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
        eglTerminate( d->Display );
        d->Display = EGL_NO_DISPLAY;
    }
    if( fghRiscosPointerHidden( ) )
        _kernel_osbyte( 106, 1, 0 );    /* the pointer back */
    if( d->Task )
    {
        r.r[0] = d->Task;
        r.r[1] = TASK_WORD;
        _kernel_swi( Wimp_CloseDown, &r, &r );
        d->Task = 0;
    }
}

void fgPlatformDestroyContext( SFG_PlatformDisplay pDisplay, SFG_WindowContextType MContext )
{
    if( MContext != EGL_NO_CONTEXT )
        eglDestroyContext( pDisplay.Display, MContext );
}


/* -- Extensions --------------------------------------------------------- */

SFG_Proc fgPlatformGetProcAddress( const char *procName )
{
    return (SFG_Proc) eglGetProcAddress( procName );
}

GLUTproc fgPlatformGetGLUTProcAddress( const char* procName )
{
    /* optimization: quick initial check */
    if( strncmp( procName, "glut", 4 ) != 0 )
        return NULL;

#define CHECK_NAME(x) if( strcmp( procName, #x ) == 0) return (GLUTproc)x;
    CHECK_NAME(glutJoystickFunc);
    CHECK_NAME(glutForceJoystickFunc);
    CHECK_NAME(glutGameModeString);
    CHECK_NAME(glutEnterGameMode);
    CHECK_NAME(glutLeaveGameMode);
    CHECK_NAME(glutGameModeGet);
#undef CHECK_NAME

    return NULL;
}


/* -- Game mode ----------------------------------------------------------- */
/*
 * Game mode is a borderless window covering the whole desktop, in the
 * current screen mode: changing the mode would change it for every task.
 * Other tasks keep running (their windows are just covered).
 */

void fgPlatformRememberState( void )
{
}

void fgPlatformRestoreState( void )
{
}

GLboolean fgPlatformChangeDisplayMode( GLboolean haveToTest )
{
    (void) haveToTest;
    fghRiscosReadScreen( );
    fgState.GameModeSize.X  = fgDisplay.ScreenWidth;
    fgState.GameModeSize.Y  = fgDisplay.ScreenHeight;
    fgState.GameModeDepth   = 32;
    fgState.GameModeRefresh = 60;
    return GL_TRUE;
}

void fgPlatformEnterGameMode( void )
{
}

void fgPlatformLeaveGameMode( void )
{
}

GLvoid fgPlatformGetGameModeVMaxExtent( SFG_Window* window, int* x, int* y )
{
    (void) window;
    *x = fgDisplay.ScreenWidth;
    *y = fgDisplay.ScreenHeight;
}


/* -- Devices RISC OS doesn't have (or we don't support yet) ------------- */

void fgPlatformInitialiseInputDevices( void )
{
}

void fgPlatformCloseInputDevices( void )
{
}

void fgPlatformDeinitialiseInputDevices( void )
{
    fgPlatformCloseInputDevices();
    fgState.InputDevsInitialised = GL_FALSE;
}

void fgPlatformRegisterDialDevice( const char *dial_device )
{
    (void) dial_device;
}

struct _serialport;
struct _serialport *fg_serial_open( const char *device )
{
    (void) device;
    return NULL;
}
void fg_serial_close( struct _serialport *port ) { (void) port; }
int  fg_serial_getchar( struct _serialport *port ) { (void) port; return EOF; }
int  fg_serial_putchar( struct _serialport *port, unsigned char ch ) { (void) port; (void) ch; return 0; }
void fg_serial_flush( struct _serialport *port ) { (void) port; }

void fgPlatformJoystickRawRead( SFG_Joystick* joy, int* buttons, float* axes )
{
    (void) axes;
    if( buttons )
        *buttons = 0;
    joy->error = GL_TRUE;
}

void fgPlatformJoystickOpen( SFG_Joystick* joy )
{
    joy->error = GL_TRUE;
}

void fgPlatformJoystickInit( SFG_Joystick *fgJoystick[], int ident )
{
    fgJoystick[ ident ]->error = GL_TRUE;
    fgJoystick[ ident ]->num_axes = fgJoystick[ ident ]->num_buttons = 0;
}

void fgPlatformJoystickClose( int ident )
{
    (void) ident;
}

void fgPlatformInitializeSpaceball( void )
{
}

void fgPlatformSpaceballClose( void )
{
}

int fgPlatformHasSpaceball( void )
{
    return 0;
}

int fgPlatformSpaceballNumButtons( void )
{
    return 0;
}

void fgPlatformSpaceballSetWindow( SFG_Window *window )
{
    (void) window;
}
