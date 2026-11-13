/* riscos-mesa: compiled into RISC3D's client (risc3d/lib/r3d_gl.c) with
 * -include, by build/build-mesa.sh (into libOSMesa).
 *
 * Why: a "mixed" program (UnixLib linked in, OpenGL and the rest loaded
 * from !MesaLibs) exports only the UnixLib functions that the libraries it
 * was linked against needed. If a newer libEGL needs a function those
 * didn't, an older program doesn't have it and stops as soon as it's
 * called (ClassiCube 0.1-test13 with RISC3D: strcspn). So libOSMesa (and libEGL) need no
 * UnixLib function beyond 20.3.5-19's (build-shared.sh checks every
 * library against build/mixed-imports/). RISC3D's client uses others;
 * these are its own (SWIs, VFP's square root, sinf/cosf, which libOSMesa
 * already needed, and GCC's isinf/isnan), the same natively and on RISC
 * OS on Linux.
 * Hidden, so not exported and no clash with UnixLib's. The headers that
 * declare the real ones come first, so their declarations are untouched.
 */
#ifndef R3D_COMPAT_H
#define R3D_COMPAT_H

#include <stddef.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <kernel.h>
#include <swis.h>

#define _swix   r3dc_swix
#define strcspn r3dc_strcspn
#define clock   r3dc_clock
#define sin     r3dc_sin
#define cos     r3dc_cos
#define sqrtf   r3dc_sqrtf
/* UnixLib's isinf and isnan call __isinff etc.: GCC's own, inline */
#undef isinf
#define isinf(x) __builtin_isinf(x)
#undef isnan
#define isnan(x) __builtin_isnan(x)

__attribute__((visibility("hidden"))) _kernel_oserror *r3dc_swix(int swi, unsigned mask, ...);
__attribute__((visibility("hidden"))) size_t r3dc_strcspn(const char *s, const char *reject);
__attribute__((visibility("hidden"))) clock_t r3dc_clock(void);
__attribute__((visibility("hidden"))) double r3dc_sin(double x);
__attribute__((visibility("hidden"))) double r3dc_cos(double x);
__attribute__((visibility("hidden"))) float r3dc_sqrtf(float x);

#endif
