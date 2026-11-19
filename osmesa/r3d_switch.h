/*
 * osmesa/r3d_switch.h - what the RISC3D switch (osmesa/r3d_switch.c) adds
 * to RISC3D's client API (risc3d/lib/r3d.h) for EGL and OSMesa's routing.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#ifndef R3D_SWITCH_H
#define R3D_SWITCH_H
#include "r3d.h"

/* the gl functions of the RISC3D route in use (in place of the client's
   r3d_gl_functions): the command stream's, or the GPU library's. Ends
   with a null name. */
const struct r3d_gl_function *r3d_functions(void);

/* 1: RISC3D's driver is a DRM device (a GPU module such as V3DRender),
   and the program's own GPU library draws for it; 0: the command stream
   (Linux3D on RISC OS on Linux), or nothing */
int r3d_in_program(void);

/* With the GPU library (r3d_in_program): the current context draws
   straight into this memory (an EGL surface's sprite or screen memory;
   rows row_bytes apart, top first, &BBGGRR words), with no copy: 1, or 0
   (it can't: it draws into its own frame, read back by r3d_swap as
   before). The memory must be 4 KB aligned, whole pages to its end, and
   stay put until r3d_draw_into(NULL, ...) or the context is destroyed.
   r3d_swap(pixels) then only waits for the frame. */
int r3d_draw_into(void *pixels, int row_bytes, int width, int height);
/* 1 if the current context draws straight into pixels */
int r3d_in_place(const void *pixels);

/* 1 if contexts can share objects (the GPU library, version 3): then
   r3d_create_shared makes one sharing share's (api as r3d_create_version's;
   major 0: the highest the GPU gives). NULL: it can't (the command stream
   shares nothing). Each thread has its own current context. */
int r3d_can_share(void);
struct r3d_context *r3d_create_shared(int api, int major, int minor, struct r3d_context *share);

#endif
