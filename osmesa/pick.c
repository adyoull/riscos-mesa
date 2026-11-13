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

/* LIBGL_ALWAYS_SOFTWARE (a system variable, as Mesa's environment
   variable) set to anything but 0, false or no: always software.
   With OS_ReadVarVal, not getenv: UnixLib's getenv of a name with a '$'
   remakes one static buffer at each call, and this runs inside programs. */
static int always_software(void)
{
    char v[8];
    int i;
    _kernel_swi_regs r;
    r.r[0] = (int) "LIBGL_ALWAYS_SOFTWARE";
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

/* api EGL_OPENGL_API or EGL_OPENGL_ES_API, the version asked for (0.0:
   none, GL 2.1 or GLES 1). In order:
   1. LIBGL_ALWAYS_SOFTWARE or RISC3D$Disable set: software.
   2. RISC3D has a driver and the version is one RISC3D can give (GL up
      to 3.1, compatibility; GLES 1.1 to 3.1): RISC3D. Whether its renderer
      gives that much is known only when the context is made: if RISC3D
      can't, the caller uses OSMesa all the same (GL 2.1, GLES 1.1, 2.0).
   3. (Reserved: a GPU driven in the program, RISCOS_MESA_V3D.)
   4. Software: OSMesa, which refuses what it can't do, as before. */
EGLAPI int EGLAPIENTRY riscos_mesa_pick_renderer(unsigned int api, int major, int minor)
{
    int ok;
    if (always_software() || !r3d_available())
        return RISCOS_MESA_SOFTWARE;
    if (api == EGL_OPENGL_ES_API)
        ok = major <= 1 || (major == 2 && minor == 0) || (major == 3 && minor <= 1);
    else if (api == EGL_OPENGL_API)
        ok = major < 3 || (major == 3 && minor <= 1);
    else
        ok = 0;
    return ok ? RISCOS_MESA_RISC3D : RISCOS_MESA_SOFTWARE;
}
