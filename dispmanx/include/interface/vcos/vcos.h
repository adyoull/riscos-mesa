/*
 * vcos.h - riscos-mesa DispmanX compatibility (MIT licence).
 *
 * The Raspberry Pi's VideoCore OS abstraction (vcos) is large; Pi example
 * code mostly uses only its assertion macros and a few helpers, reached
 * indirectly (the Pi's bcm_host.h and EGL headers include vcos.h). This
 * header supplies that small subset, with the same names, so such code
 * builds unchanged. Threads, events, logging and the rest of vcos are not
 * provided: use pthreads (built into UnixLib) instead.
 */
#ifndef VCOS_H
#define VCOS_H

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>        /* the Pi's vcos.h brings these in too */
#include <unistd.h>

#define vcos_assert(cond)            assert(cond)
#define vcos_assert_msg(cond, ...)   assert(cond)
#define vc_assert(cond)              assert(cond)
#define vcos_verify(cond)            (cond)
#define vcos_demand(cond)            do { if (!(cond)) abort(); } while (0)
#define vcos_unused(x)               ((void) (x))
#define VCOS_UNUSED(x)               ((void) (x))
#define vcos_countof(x)              (sizeof(x) / sizeof((x)[0]))
#ifndef countof
#define countof(x)                   (sizeof(x) / sizeof((x)[0]))
#endif
#define vcos_min(a, b)               ((a) < (b) ? (a) : (b))
#define vcos_max(a, b)               ((a) > (b) ? (a) : (b))

typedef enum { VCOS_SUCCESS = 0, VCOS_EAGAIN, VCOS_ENOENT, VCOS_ENOSPC,
               VCOS_EINVAL, VCOS_EACCESS, VCOS_ENOMEM, VCOS_ENOSYS } VCOS_STATUS_T;

static inline void vcos_sleep(uint32_t ms) { usleep(ms * 1000u); }

#endif
