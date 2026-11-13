/*
 * osmesa/pick.c - riscos-mesa's one choice of renderer.
 *
 * riscos_mesa_pick_renderer (EGL/eglext_riscos.h) is in libOSMesa, the
 * library EGL and SDL sit on, so that OSMesa contexts (osmesa/
 * risc3d_route.c), eglCreateContext and SDL's GL windows all follow the
 * same choice. It reads only system variables and asks the RISC3D module,
 * so it is the same natively and on RISC OS on Linux.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */

#include <string.h>
#include <kernel.h>
#include <swis.h>
#include <EGL/egl.h>
#include <EGL/eglext_riscos.h>
#include "r3d.h"

/* A system variable set to anything but 0, false or no (as Mesa reads
   LIBGL_ALWAYS_SOFTWARE). With OS_ReadVarVal, not getenv: UnixLib's getenv
   of a name with a '$' remakes one static buffer at each call, and this
   runs inside programs. */
static int var_set(const char *name)
{
    char v[8];
    int i;
    _kernel_swi_regs r;
    r.r[0] = (int) name;
    r.r[1] = (int) v;
    r.r[2] = (int) sizeof v - 1;
    r.r[3] = 0;
    r.r[4] = 3;
    if (_kernel_swi(OS_ReadVarVal, &r, &r) != NULL || r.r[2] <= 0 || r.r[2] >= (int) sizeof v)
        return r.r[2] >= (int) sizeof v;    /* too long to be "0": set */
    v[r.r[2]] = 0;
    for (i = 0; v[i]; i++)
        if (v[i] >= 'A' && v[i] <= 'Z')
            v[i] += 'a' - 'A';
    return strcmp(v, "0") && strcmp(v, "false") && strcmp(v, "no");
}

/* RISC3D_Info's r4 (RISC3D 0.03 with Linux3D 0.02 and rol-3d 0.7 set bit
   1 when the driver draws in software: Linux's Mesa with no GPU driver),
   or 0 if it can't be read. Read directly, so any client library works. */
#define RISC3D_INFO      0xC3D00
#define DRIVER_SOFTWARE  2
static unsigned int driver_flags(void)
{
    _kernel_swi_regs r;
    memset(&r, 0, sizeof r);
    if (_kernel_swi(RISC3D_INFO, &r, &r) != NULL || !r.r[3])
        return 0;
    return (unsigned int) r.r[4];
}

/* api EGL_OPENGL_API or EGL_OPENGL_ES_API, the version asked for (0.0:
   none, GL 2.1 or GLES 1). In order:
   1. LIBGL_ALWAYS_SOFTWARE or RISC3D$Disable set: software.
   2. RISC3D has a driver and the version is one RISC3D can give (GL up
      to 3.1, compatibility; GLES 1.1 to 3.1): RISC3D. Whether its renderer
      gives that much is known only when the context is made: if RISC3D
      can't, the caller uses OSMesa all the same (GL 2.1, GLES 1.1, 2.0).
      But if RISC3D's driver draws in software itself (RISC3D_Info bit 1)
      only what OSMesa can't give goes to it (GL 3, GLES 3): our own
      renderer is much faster than another software renderer whose frames
      are read back. RISC3D$Force set sends everything to RISC3D anyway.
   3. (Reserved: a GPU driven in the program, RISCOS_MESA_V3D.)
   4. Software: OSMesa, which refuses what it can't do, as before. */
EGLAPI int EGLAPIENTRY riscos_mesa_pick_renderer(unsigned int api, int major, int minor)
{
    int ok, only_risc3d;
    if (var_set("LIBGL_ALWAYS_SOFTWARE") || !r3d_available())
        return RISCOS_MESA_SOFTWARE;
    if (api == EGL_OPENGL_ES_API) {
        ok = major <= 1 || (major == 2 && minor == 0) || (major == 3 && minor <= 1);
        only_risc3d = major == 3;
    } else if (api == EGL_OPENGL_API) {
        ok = major < 3 || (major == 3 && minor <= 1);
        only_risc3d = major == 3;
    } else {
        ok = only_risc3d = 0;
    }
    if (ok && !only_risc3d && (driver_flags() & DRIVER_SOFTWARE) && !var_set("RISC3D$Force"))
        ok = 0;
    return ok ? RISCOS_MESA_RISC3D : RISCOS_MESA_SOFTWARE;
}
