/*
 * osmesa/r3d_stream_names.h - RISC3D's client (risc3d/lib/r3d_gl.c, which
 * writes RISC3D's command stream) compiled under other names, so that
 * the RISC3D switch (osmesa/r3d_switch.c) can give the client's API
 * (r3d.h) to EGL and OSMesa: the command stream where RISC3D's driver
 * takes one (Linux3D on RISC OS on Linux), the GPU library where it is a
 * DRM device (V3DRender natively). Used with -include when r3d_gl.c is
 * compiled (build/build-mesa.sh); RISC3D's source is unchanged.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#define r3d_gl_functions    r3ds_gl_functions
#define r3d_available       r3ds_available
#define r3d_create          r3ds_create
#define r3d_create_gl       r3ds_create_gl
#define r3d_create_es1      r3ds_create_es1
#define r3d_create_version  r3ds_create_version
#define r3d_target          r3ds_target
#define r3d_target_copy     r3ds_target_copy
#define r3d_driver_flags    r3ds_driver_flags
#define r3d_uploads         r3ds_uploads
#define r3d_make_current    r3ds_make_current
#define r3d_destroy         r3ds_destroy
#define r3d_open            r3ds_open
#define r3d_open_gl         r3ds_open_gl
#define r3d_open_es1        r3ds_open_es1
#define r3d_close           r3ds_close
#define r3d_surface         r3ds_surface
#define r3d_swap            r3ds_swap
#define r3d_swap_overlapped r3ds_swap_overlapped
#define r3d_overlay         r3ds_overlay
#define r3d_swap_overlay    r3ds_swap_overlay
#define r3d_flush           r3ds_flush
#define r3d_stats           r3ds_stats
#define r3d_error           r3ds_error
#define r3d_create_shared   r3ds_create_shared
#define r3d_can_share       r3ds_can_share
