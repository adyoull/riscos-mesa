/* r3d.h: RISC3D for programs. OpenGL 1.x/2.1 (the compatibility profile)
   or GLES 2, drawn by whatever backend the RISC3D module has: on RISC OS
   on Linux, Linux's graphics driver.

   A program opens one context, gives it a surface size, draws with the gl
   functions, and takes each finished frame as pixels (r3d_swap): 32 bits a
   pixel in a RISC OS sprite's order (&BBGGRR words), top row first.

   PROTOTYPE: the SWI numbers are provisional and the interface will change.

   Copyright (c) 2026 Andrew Youll. MIT licence (see LICENSE). */
#ifndef R3D_H
#define R3D_H
#include <stddef.h>

#define RISC3D_Info	0xC3D00		/* provisional chunk */
#define RISC3D_Register	0xC3D01
#define RISC3D_Open	0xC3D02
#define RISC3D_Close	0xC3D03
#define RISC3D_Sync	0xC3D04

/* For Mesa's EGL (the real front end): the gl functions are r3d_glXxx,
   listed by their GL names so that a dispatch table can be built from
   them; contexts are made, made current and destroyed; r3d_swap finishes
   the current context's frame into a buffer. */
struct r3d_gl_function { const char *name; void (*fn) (void); };
extern const struct r3d_gl_function r3d_gl_functions[];	/* ends with a null name */
struct r3d_context;
int r3d_available (void);		/* a driver is registered, and RISC3D$Disable isn't set */
struct r3d_context *r3d_create (void);	/* GLES 2. 0: see r3d_error */
/* the APIs (OP_CONTEXT's word) */
#define R3D_API_GLES2 0
#define R3D_API_GL 1
#define R3D_API_GLES1 2

struct r3d_context *r3d_create_gl (void); /* OpenGL (compatibility profile). 0: see r3d_error */
struct r3d_context *r3d_create_es1 (void); /* OpenGL ES 1.1 (from RISC3D 0.4). 0: see r3d_error */
void r3d_make_current (struct r3d_context *);
void r3d_destroy (struct r3d_context *);

/* For test programs: after "r3d_gl.h" (or GL/gl.h with glext.h), define
   R3D_DIRECT before this header and the program's own glXxx calls are
   RISC3D's. */
#ifdef R3D_DIRECT
#include "r3d_gl_names.h"
#endif

/* The simple form (one context). 0, or a RISC OS error's text (no module,
   no driver, no Linux helper) */
const char *r3d_open (void);		/* GLES 2 */
const char *r3d_open_gl (void);		/* OpenGL */
const char *r3d_open_es1 (void);		/* OpenGL ES 1.1 */
void r3d_close (void);
/* the size of what is drawn on, in pixels */
void r3d_surface (int width, int height);
/* finishes the frame and copies it out: width * height * 4 bytes.
   Returns the bytes copied, or -1 */
int r3d_swap (void *pixels, size_t bytes);
/* the same, overlapped: the renderer finishes and reads back this frame
   while the program draws the next, and the frame that comes back is the
   one before (no wait for the GPU; a frame of latency). Not to be mixed
   with r3d_swap in one context. */
int r3d_swap_overlapped (void *pixels, size_t bytes);
/* Zero-copy frames, where the renderer can put them on a display plane
   (RISC OS on Linux: rol-fb's overlays). The picture is under the
   desktop and shows only where the desktop's pixels are the key colour
   (&080008 in a 16 million colour mode): the program paints its window's
   work area in that colour, and windows over it clip the picture for
   nothing. r3d_overlay places it: x, y, width, height in RISC OS pixels
   from the top left of the screen (the window's visible area; the frame
   is scaled to fit); width 0 hides it. Call it when the window opens,
   moves or changes size, and with width 0 when it closes or is hidden.
   Returns 1 (frames go to the screen: key set to the colour to paint), 0
   (they can't: stay with r3d_swap), -1 (no context).
   r3d_swap_overlay then ends each frame (the program waits for the frame
   before to be on the screen: the display's rate). Returns 0, or -1: the
   overlay has stopped (a mode change, say): go back to r3d_swap and
   repaint the window; r3d_overlay may be tried again. Don't mix with
   r3d_swap_overlapped's frames except after such a fall back. */
int r3d_overlay (int x, int y, int width, int height, unsigned *key);
int r3d_swap_overlay (void);
/* lets the renderer see everything so far (nothing comes back) */
void r3d_flush (void);
/* for measuring: the renderer's time since the last call (ns working,
   ns of that reading frames back, sleeps, commands). 0, or -1 */
int r3d_stats (unsigned long long v[4]);
/* the last RISC OS error from the module, or 0 */
const char *r3d_error (void);

#endif
