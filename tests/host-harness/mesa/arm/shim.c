/*
 * shim.c - lets the RISC OS build of libOSMesa.a link into an ARM Linux
 * program, so the real cross-compiled Mesa can be run under qemu-arm
 * (arm/run-arm.sh).  Mesa's rendering code is plain C and the ABI
 * (arm-*-gnueabihf) is the same; only a few UnixLib symbols differ:
 *   - __stdin/__stdout/__stderr: UnixLib's names for the standard streams;
 *   - __ctype: UnixLib's character class table (a pointer, flags below);
 *   - errno: a plain global in UnixLib but thread-local in glibc, so
 *     run-arm.sh renames Mesa's references to ro_errno_shim.
 *   - pthread mutexes: UnixLib lays them out differently from glibc (and
 *     its mutex type numbers differ), so glibc aborted in pthread_mutex_lock
 *     now and then. The checks use one thread: run-arm.sh renames Mesa's
 *     mutex calls to the do-nothing ro_pthread_*_shim functions below.
 *   - _kernel_swi: only VFPSupport_Features, which the shader interpreter
 *     asks whether the CPU has NEON. It answers as a Cortex-A8 with NEON
 *     (qemu-arm -cpu cortex-a8 has it); MESA_NO_NEON=1 makes Mesa use the
 *     C code, as on a machine without NEON.
 * Part of riscos-mesa, MIT licence.
 */
#include <stdio.h>
#include <ctype.h>
int ro_errno_shim;
int ro_pthread_mutex_init_shim(void *m, const void *a) { (void) m; (void) a; return 0; }
int ro_pthread_mutex_lock_shim(void *m) { (void) m; return 0; }
int ro_pthread_mutex_unlock_shim(void *m) { (void) m; return 0; }
int ro_pthread_mutex_destroy_shim(void *m) { (void) m; return 0; }
int ro_pthread_mutexattr_init_shim(void *a) { (void) a; return 0; }
int ro_pthread_mutexattr_settype_shim(void *a, int t) { (void) a; (void) t; return 0; }
int ro_pthread_mutexattr_destroy_shim(void *a) { (void) a; return 0; }
typedef struct { int r[10]; } ro_kernel_swi_regs;
static int no_swi_error[2] = { 0x1E6, 0 };   /* "SWI not known" */
void *_kernel_swi(int no, ro_kernel_swi_regs *in, ro_kernel_swi_regs *out)
{
    if (no == 0x58EC8 && in->r[0] == 0) {    /* VFPSupport_Features 0 */
        out->r[0] = 0x410330C3;              /* FPSID, MVFR0, MVFR1 of */
        out->r[1] = 0x11110222;              /* a Cortex-A8 */
        out->r[2] = 0x00011111;
        return 0;
    }
    return no_swi_error;
}
FILE *__stdin, *__stdout, *__stderr;
static unsigned char tbl[384];
const unsigned char *const __ctype = tbl + 128;
__attribute__((constructor)) static void init(void) {
    int c;
    __stdin = stdin;
    __stdout = stdout;
    __stderr = stderr;
    for (c = 0; c < 256; c++) {
        unsigned char f = 0;
        if (iscntrl(c))
            f |= 1;
        if (isupper(c))
            f |= 2;
        if (islower(c))
            f |= 4;
        if (isalpha(c))
            f |= 8;
        if (ispunct(c))
            f |= 16;
        if (isspace(c))
            f |= 32;
        if (isdigit(c))
            f |= 64;
        if (isxdigit(c))
            f |= 128;
        tbl[128 + c] = f;
    }
}
