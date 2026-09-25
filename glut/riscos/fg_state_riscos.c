/*
 * fg_state_riscos.c
 *
 * The RISC OS back end of freeglut: glutGet and glutDeviceGet queries.
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

#include <GL/freeglut.h>
#include "../fg_internal.h"

extern int fghRiscosDisplayModePossible( void );
extern int fghRiscosFrameSize( SFG_Window *window, int header );

static int fghConfigAttrib( EGLint attribute )
{
    EGLint value = 0;
    SFG_Window *window = fgStructure.CurrentWindow;
    if( window && window->Window.pContext.Config )
        eglGetConfigAttrib( fgDisplay.pDisplay.Display, window->Window.pContext.Config,
                            attribute, &value );
    return value;
}

int fgPlatformGlutGet( GLenum eWhat )
{
    SFG_Window *window = fgStructure.CurrentWindow;

    switch( eWhat )
    {
    case GLUT_WINDOW_X:
    case GLUT_WINDOW_Y:
        if( !window )
            return 0;
        /* top level: client area on the screen; subwindow: in its parent */
        return eWhat == GLUT_WINDOW_X ? window->State.Xpos : window->State.Ypos;

    case GLUT_WINDOW_WIDTH:
        return window ? window->State.Width : 0;
    case GLUT_WINDOW_HEIGHT:
        return window ? window->State.Height : 0;

    case GLUT_WINDOW_BORDER_WIDTH:
        return ( window && !window->Parent ) ? fghRiscosFrameSize( window, 0 ) : 0;
    case GLUT_WINDOW_HEADER_HEIGHT:
        return ( window && !window->Parent ) ? fghRiscosFrameSize( window, 1 ) : 0;

    case GLUT_WINDOW_BUFFER_SIZE:   return fghConfigAttrib( EGL_BUFFER_SIZE );
    case GLUT_WINDOW_STENCIL_SIZE:  return fghConfigAttrib( EGL_STENCIL_SIZE );
    case GLUT_WINDOW_DEPTH_SIZE:    return fghConfigAttrib( EGL_DEPTH_SIZE );
    case GLUT_WINDOW_RED_SIZE:      return fghConfigAttrib( EGL_RED_SIZE );
    case GLUT_WINDOW_GREEN_SIZE:    return fghConfigAttrib( EGL_GREEN_SIZE );
    case GLUT_WINDOW_BLUE_SIZE:     return fghConfigAttrib( EGL_BLUE_SIZE );
    case GLUT_WINDOW_ALPHA_SIZE:    return fghConfigAttrib( EGL_ALPHA_SIZE );
    case GLUT_WINDOW_FORMAT_ID:     return fghConfigAttrib( EGL_CONFIG_ID );

    case GLUT_WINDOW_ACCUM_RED_SIZE:
    case GLUT_WINDOW_ACCUM_GREEN_SIZE:
    case GLUT_WINDOW_ACCUM_BLUE_SIZE:
    case GLUT_WINDOW_ACCUM_ALPHA_SIZE:
    case GLUT_WINDOW_NUM_SAMPLES:
    case GLUT_WINDOW_STEREO:
    case GLUT_WINDOW_COLORMAP_SIZE:
        return 0;

    case GLUT_WINDOW_RGBA:
        return 1;

    case GLUT_WINDOW_DOUBLEBUFFER:
        return window ? window->Window.DoubleBuffered : 0;

    case GLUT_DISPLAY_MODE_POSSIBLE:
        return fghRiscosDisplayModePossible( );

    default:
        fgWarning( "glutGet(): missing enum handle %d", eWhat );
        return -1;
    }
}

int fgPlatformGlutDeviceGet( GLenum eWhat )
{
    switch( eWhat )
    {
    case GLUT_HAS_KEYBOARD:
    case GLUT_HAS_MOUSE:
        return 1;
    case GLUT_NUM_MOUSE_BUTTONS:
        return 3;
    default:
        fgWarning( "glutDeviceGet(): missing enum handle %d", eWhat );
        return -1;
    }
}

int *fgPlatformGlutGetModeValues( GLenum eWhat, int *size )
{
    (void) eWhat;
    *size = 0;                          /* no multisampling, no aux buffers */
    return NULL;
}

/* No colour index windows */
void fgPlatformSetColor( int idx, float r, float g, float b )
{
    (void) idx; (void) r; (void) g; (void) b;
}

float fgPlatformGetColor( int idx, int comp )
{
    (void) idx; (void) comp;
    return -1.0f;
}

void fgPlatformCopyColormap( int win )
{
    (void) win;
}
