/* riscos-mesa: RISC3D's client's own copies of _swix, strcspn, clock,
 * sin, cos and sqrtf (see r3d_compat.h for why). */
#include <stdarg.h>
#include <math.h>
#include <stddef.h>
#include <time.h>
#include <kernel.h>
#include <swis.h>
#include "r3d_compat.h"

/* _swix for the masks RISC3D's client uses: registers in (_IN, _INR) and
   out (_OUT, _OUTR) only. The arguments are the input registers' values,
   lowest register first, then pointers for the outputs, lowest first. */
_kernel_oserror *r3dc_swix(int swi, unsigned mask, ...)
{
    static _kernel_oserror unsupported = { 0, "r3dc_swix: unsupported mask" };
    _kernel_swi_regs r;
    _kernel_oserror *e;
    va_list ap;
    int i;

    if (mask & 0x003FFC00u)     /* _BLOCK, _RETURN, _FLAGS, ...: not used */
        return &unsupported;
    va_start(ap, mask);
    for (i = 0; i < 10; i++)
        r.r[i] = (mask & (1u << i)) ? va_arg(ap, int) : 0;
    e = _kernel_swi(swi | 0x20000, &r, &r);     /* X form */
    if (!e)
        for (i = 0; i < 10; i++)
            if (mask & (1u << (31 - i)))
                *va_arg(ap, int *) = r.r[i];
    va_end(ap);
    return e;
}

size_t r3dc_strcspn(const char *s, const char *reject)
{
    const char *p, *q;
    for (p = s; *p; p++)
        for (q = reject; *q; q++)
            if (*p == *q)
                return (size_t) (p - s);
    return (size_t) (p - s);
}

/* For timeouts only: centiseconds since the machine started
   (OS_ReadMonotonicTime), in CLOCKS_PER_SEC units. */
clock_t r3dc_clock(void)
{
    _kernel_swi_regs r;
    if (_kernel_swi(0x42 | 0x20000, &r, &r))
        return (clock_t) -1;
    return (clock_t) ((unsigned) r.r[0] * (CLOCKS_PER_SEC / 100));
}

/* glRotate's angle: float precision is all GL keeps */
double r3dc_sin(double x) { return sinf((float) x); }
double r3dc_cos(double x) { return cosf((float) x); }

/* VFP's VSQRT (build-egl.sh compiles this with -fno-math-errno: no call) */
float r3dc_sqrtf(float x) { return __builtin_sqrtf(x); }
