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

#endif
