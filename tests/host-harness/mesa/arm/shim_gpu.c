/*
 * shim_gpu.c - with shim.c, lets the RISC OS build of the GPU library
 * (libMesaGPU.a: Mesa 26's v3d driver) and libOSMesa.a link into an ARM
 * Linux program, to run under qemu-arm with V3DM$Fake (drm_riscos_fake.c:
 * no GPU; jobs finish at once). What runs is the real cross-compiled
 * driver, so its CPU cost can be measured and profiled (arm/prof-gpu.sh).
 *
 * The UnixLib symbols it adds to shim.c's:
 *   - pthreads: UnixLib's types differ from glibc's, so prof-gpu.sh renames
 *     every pthread_ call to ro_pthread_*_shim below. One thread: mutexes
 *     and condition variables do nothing, keys are a table, once runs once,
 *     and threads can't be made (EAGAIN).
 *   - _swix: OS_ReadVarVal (getenv), OS_ReadMonotonicTime; anything else
 *     answers "SWI not known", so the module's paths aren't taken.
 *   - _kernel_swi: OS_ReadVarVal too (pick.c, r3d_switch.c), then shim.c's.
 *   - __unixlib_fstat64, __ctype_upper, readlink, sysconf.
 * Part of riscos-mesa, MIT licence.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---- pthreads, one thread ---- */
#define S(name) ro_pthread_##name##_shim
int S(cond_broadcast)(void *c) { (void) c; return 0; }
int S(cond_destroy)(void *c) { (void) c; return 0; }
int S(cond_init)(void *c, const void *a) { (void) c; (void) a; return 0; }
int S(cond_signal)(void *c) { (void) c; return 0; }
int S(cond_timedwait)(void *c, void *m, const void *t) { (void) c; (void) m; (void) t; return ETIMEDOUT; }
int S(cond_wait)(void *c, void *m) { (void) c; (void) m; return 0; }
int S(condattr_destroy)(void *a) { (void) a; return 0; }
int S(condattr_init)(void *a) { (void) a; return 0; }
int S(condattr_setclock)(void *a, int c) { (void) a; (void) c; return 0; }
int S(create)(void *t, const void *a, void *(*f)(void *), void *arg) { (void) t; (void) a; (void) f; (void) arg; return EAGAIN; }
int S(detach)(unsigned long t) { (void) t; return 0; }
int S(equal)(unsigned long a, unsigned long b) { return a == b; }
void S(exit)(void *r) { (void) r; exit(0); }
int S(join)(unsigned long t, void **r) { (void) t; (void) r; return ESRCH; }
unsigned long S(self)(void) { return 1; }
int S(sigmask)(int h, const void *s, void *o) { (void) h; (void) s; (void) o; return 0; }
int S(mutex_trylock)(void *m) { (void) m; return 0; }
static void *keys[256];
static int nkeys;
int S(key_create)(int *k, void (*d)(void *)) { (void) d; if (nkeys >= 256) return EAGAIN; *k = nkeys++; return 0; }
int S(key_delete)(int k) { (void) k; return 0; }
void *S(getspecific)(int k) { return k >= 0 && k < 256 ? keys[k] : NULL; }
int S(setspecific)(int k, const void *v) { if (k < 0 || k >= 256) return EINVAL; keys[k] = (void *) v; return 0; }
int S(once)(int *o, void (*f)(void)) { if (!*o) { *o = 1; f(); } return 0; }

/* ---- SWIs ---- */
typedef struct { int r[10]; } regs;
static int not_known[2] = { 0x1E6, 0 };
static int read_var(regs *r)
{
    const char *v = getenv((const char *) r->r[0]);
    int n;
    if (!v)
        return 0;
    n = (int) strlen(v);
    if (n > r->r[2])
        n = r->r[2];
    memcpy((void *) r->r[1], v, n);
    r->r[2] = n;
    return 1;
}
void *ro_kernel_swi_shim(int no, regs *in, regs *out);
void *_kernel_swi(int no, regs *in, regs *out)
{
    if ((no & ~0x20000) == 0x23) {          /* OS_ReadVarVal */
        *out = *in;
        if (read_var(out))
            return 0;
        out->r[2] = 0;
        return not_known;
    }
    return ro_kernel_swi_shim(no, in, out);
}
void *_swix(int no, unsigned mask, ...)
{
    va_list a;
    int in[10] = { 0 }, i, *p;
    va_start(a, mask);
    for (i = 0; i < 10; i++)
        if (mask & (1u << i))
            in[i] = va_arg(a, int);
    if ((no & ~0x20000) == 0x42) {          /* OS_ReadMonotonicTime */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        in[0] = (int) (ts.tv_sec * 100 + ts.tv_nsec / 10000000);
    } else if ((no & ~0x20000) == 0x23) {
        regs r;
        memcpy(r.r, in, sizeof in);
        if (!read_var(&r)) { va_end(a); return not_known; }
        memcpy(in, r.r, sizeof in);
    } else {
        va_end(a);
        return not_known;
    }
    for (i = 0; i < 10; i++)                /* _OUT(n) is bit 31 - n */
        if (mask & (1u << (31 - i))) {
            p = va_arg(a, int *);
            *p = in[i];
        }
    va_end(a);
    return 0;
}

/* ---- the rest ---- */
int __unixlib_fstat64(int fd, struct stat *st) { return fstat(fd, st); }
static short upper[384];
const short *const __ctype_upper = upper + 128;
__attribute__((constructor)) static void init_upper(void)
{
    int c;
    for (c = -128; c < 256; c++)
        upper[128 + c] = c >= 'a' && c <= 'z' ? c - 32 : c;
}

/* memcpy and memchr: glibc's static ones are IFUNCs, and the RISC OS
   objects' calls reached the resolver itself (it ran as memcpy). Plain
   ones here are linked instead. */
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if ((((unsigned long) dp | (unsigned long) sp) & 3) == 0) {
        for (; n >= 4; n -= 4, dp += 4, sp += 4)
            *(unsigned *) dp = *(const unsigned *) sp;
    }
    while (n--)
        *dp++ = *sp++;
    return d;
}
void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (; n; n--, p++)
        if (*p == (unsigned char) c)
            return (void *) p;
    return NULL;
}
