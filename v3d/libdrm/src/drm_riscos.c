/*
 * drm_riscos.c - the part of libdrm that Mesa's GPU drivers use, on RISC
 * OS: each call becomes a request to the GPU's RISC OS driver module
 * through its ioctl SWI, which takes Linux's request numbers and
 * structures (for the Pi 4's V3D: V3DRender's docs/INTERFACE.md).
 *
 * - The device: RISC3D lists GPU driver modules of the "DRM device" kind
 *   (RISC3D_Info: r4 bit 0, r5 = the module's ioctl SWI, r6 -> its DRM
 *   driver name, such as "v3d"), and RISC3D_Open gives the file
 *   (r1). Programs never call a GPU module's own SWIs other than the
 *   ioctl SWI RISC3D names: without RISC3D listing a DRM device there is
 *   no GPU.
 * - A "file" is the module's number, not a UnixLib descriptor: close it
 *   with drm_riscos_close, never close().
 * - drmGetVersion asks the module (DRM_IOCTL_VERSION, as on Linux); a
 *   module that doesn't answer it is taken as version 1.0 of the name
 *   RISC3D gave.
 * - A BO's mmap offset is already its address: drm_riscos_mmap returns
 *   it, and drm_riscos_munmap does nothing (the riscos-v3d patch points
 *   Mesa's mmap/munmap/close of these at them).
 * - Sync objects are requests as on Linux. drmSyncobjWait's timeout is
 *   absolute on Mesa's CLOCK_MONOTONIC: it is moved onto the modules'
 *   clock (OS_ReadMonotonicTime * 10,000,000 ns).
 * - Not offered: PRIME (dma-buf) import/export, timeline sync objects.
 *   They fail with EINVAL or ENOSYS, and Mesa doesn't need them for
 *   drawing.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <kernel.h>
#include <swis.h>

#include <stdio.h>
#include "xf86drm.h"
#include "drm-uapi/v3d_drm.h"   /* (V3DM$Stats: CREATE_BO sizes) */
#include "drm_riscos.h"
#include "drm_riscos_fake.h"
#include "drm_riscos_dump.h"

/* RISC3D's SWIs, allocated (RISC OS Allocations, 18 Nov 2026; the same
   numbers as risc3d/lib/r3d.h, client 0.11). RISC3D 0.05 answers only these. */
#define RISC3D_Info     0x5AF80
#define RISC3D_Open     0x5AF82
#define RISC3D_Close    0x5AF83
#ifndef OS_ReadMonotonicTime
#define OS_ReadMonotonicTime 0x42
#endif

/* ---- V3DM$Fake (no V3D: drm_riscos_fake.c) and V3DM$Trace (each
   request on stderr), read once ---- */

static int fake = -1, trace, stats;

/* (getenv: only whether it is set is kept, never the pointer, which
   UnixLib reuses for the next system variable read) */
static int var_set(const char *name)
{
    return getenv(name) != NULL;
}

static void modes(void)
{
    if (fake < 0) {
        fake = var_set("V3DM$Fake");
        trace = var_set("V3DM$Trace");
        stats = var_set("V3DM$Stats");
    }
}

/* ---- the device ---- */

static struct {
    int found;              /* 1: looked for; name[0]: one was found */
    char name[32];          /* its DRM driver name ("v3d") */
    unsigned ioctl_swi;
} device;

/* the program's file (sync files belong to it), and its RISC3D context */
static int program_file, program_context;

static void set_name(const char *n)
{
    size_t l = 0;
    while (n[l] >= ' ' && l < sizeof device.name - 1)
        l++;
    memcpy(device.name, n, l);
    device.name[l] = 0;
}

static void find_device(void)
{
    unsigned flags = 0, swi = 0;
    const char *driver = NULL, *drm_name = NULL;
    modes();
    if (device.found)
        return;
    device.found = 1;
    if (fake) {
        strcpy(device.name, "v3d");
        return;
    }
    /* RISC3D's choice, if it is a DRM device */
    if (!_swix(RISC3D_Info, _OUTR(3, 6), &driver, &flags, &swi, &drm_name) &&
        driver && (flags & 1) && swi && drm_name) {
        set_name(drm_name);
        device.ioctl_swi = swi;
    }
}

const char *drm_riscos_device(void)
{
    find_device();
    return device.name[0] ? device.name : NULL;
}

int drm_riscos_available(void)
{
    return drm_riscos_device() != NULL;
}

/* The program's file is closed when it exits (exit() or returning from
   main), so its GPU memory and RISC3D's record of it go then. A program
   that isn't a Wimp task (a child process, a command) has no close-down
   for RISC3D to clean up after (its contexts were left, listed under task
   0 until a restart, 21 Nov 2026). Mesa's clean-up after that
   (another atexit handler, a library's destructor) finds every request
   answered with success, doing nothing. */
static int closed_at_exit;

static void close_at_exit(void)
{
    if (program_file > 0 && program_context && !fake)
        _swix(RISC3D_Close, _IN(0), program_context);
    program_file = program_context = 0;
    closed_at_exit = 1;
}

int drm_riscos_open(void)
{
    static int atexit_done;
    int fd = 0, context = 0;
    find_device();
    if (fake)
        fd = 1;
    else if (!device.name[0] || _swix(RISC3D_Open, _IN(0) | _OUTR(0, 1), 0, &context, &fd))
        fd = 0;
    if (fd <= 0) {
        errno = ENODEV;
        return -1;
    }
    if (program_file <= 0) {
        program_file = fd;
        program_context = context;
    }
    if (!atexit_done) {
        atexit(close_at_exit);
        atexit_done = 1;
    }
    closed_at_exit = 0;
    return fd;
}

int drm_riscos_close(int fd)
{
    modes();
    if (fd > 0 && !fake && fd == program_file && program_context)
        _swix(RISC3D_Close, _IN(0), program_context);       /* closes the module's file too */
    if (fd == program_file)
        program_file = program_context = 0;
    return 0;
}

void *drm_riscos_mmap(void *addr, size_t size, int prot, int flags, int fd, uint64_t offset)
{
    (void) addr; (void) size; (void) prot; (void) flags; (void) fd;
    return (void *) (uintptr_t) offset;
}

int drm_riscos_munmap(void *addr, size_t size)
{
    (void) addr; (void) size;
    return 0;
}

/* GPU modules answer with Linux's error numbers (-errno: V3DRender's
   docs/INTERFACE.md), but Mesa is compiled with UnixLib's <errno.h>, whose
   numbers differ above 10: Linux's ETIME (62) is UnixLib's ELOOP, so a
   timed-out wait (v3d_bo_wait with timeout 0, as a busy check) looked like
   a real failure and Mesa aborted ("wait failed: -62": YSFlight, 16 Nov
   2026). Each number Mesa can see is turned into UnixLib's. */
static int from_linux_errno(int e)
{
    switch (e) {
#ifdef EPERM
    case 1: return EPERM;
#endif
#ifdef ENOENT
    case 2: return ENOENT;
#endif
#ifdef ESRCH
    case 3: return ESRCH;
#endif
#ifdef EINTR
    case 4: return EINTR;
#endif
#ifdef EIO
    case 5: return EIO;
#endif
#ifdef ENXIO
    case 6: return ENXIO;
#endif
#ifdef E2BIG
    case 7: return E2BIG;
#endif
#ifdef ENOEXEC
    case 8: return ENOEXEC;
#endif
#ifdef EBADF
    case 9: return EBADF;
#endif
#ifdef ECHILD
    case 10: return ECHILD;
#endif
#ifdef EAGAIN
    case 11: return EAGAIN;
#endif
#ifdef ENOMEM
    case 12: return ENOMEM;
#endif
#ifdef EACCES
    case 13: return EACCES;
#endif
#ifdef EFAULT
    case 14: return EFAULT;
#endif
#ifdef ENOTBLK
    case 15: return ENOTBLK;
#endif
#ifdef EBUSY
    case 16: return EBUSY;
#endif
#ifdef EEXIST
    case 17: return EEXIST;
#endif
#ifdef EXDEV
    case 18: return EXDEV;
#endif
#ifdef ENODEV
    case 19: return ENODEV;
#endif
#ifdef ENOTDIR
    case 20: return ENOTDIR;
#endif
#ifdef EISDIR
    case 21: return EISDIR;
#endif
#ifdef EINVAL
    case 22: return EINVAL;
#endif
#ifdef ENFILE
    case 23: return ENFILE;
#endif
#ifdef EMFILE
    case 24: return EMFILE;
#endif
#ifdef ENOTTY
    case 25: return ENOTTY;
#endif
#ifdef ETXTBSY
    case 26: return ETXTBSY;
#endif
#ifdef EFBIG
    case 27: return EFBIG;
#endif
#ifdef ENOSPC
    case 28: return ENOSPC;
#endif
#ifdef ESPIPE
    case 29: return ESPIPE;
#endif
#ifdef EROFS
    case 30: return EROFS;
#endif
#ifdef EMLINK
    case 31: return EMLINK;
#endif
#ifdef EPIPE
    case 32: return EPIPE;
#endif
#ifdef EDOM
    case 33: return EDOM;
#endif
#ifdef ERANGE
    case 34: return ERANGE;
#endif
#ifdef EDEADLK
    case 35: return EDEADLK;
#endif
#ifdef ENAMETOOLONG
    case 36: return ENAMETOOLONG;
#endif
#ifdef ENOLCK
    case 37: return ENOLCK;
#endif
#ifdef ENOSYS
    case 38: return ENOSYS;
#endif
#ifdef ENOTEMPTY
    case 39: return ENOTEMPTY;
#endif
#ifdef ELOOP
    case 40: return ELOOP;
#endif
#ifdef ENOMSG
    case 42: return ENOMSG;
#endif
#ifdef EIDRM
    case 43: return EIDRM;
#endif
#ifdef ENOSTR
    case 60: return ENOSTR;
#endif
#ifdef ENODATA
    case 61: return ENODATA;
#endif
#ifdef ETIME
    case 62: return ETIME;
#endif
#ifdef ENOSR
    case 63: return ENOSR;
#endif
#ifdef ENOLINK
    case 67: return ENOLINK;
#endif
#ifdef EPROTO
    case 71: return EPROTO;
#endif
#ifdef EBADMSG
    case 74: return EBADMSG;
#endif
#ifdef EOVERFLOW
    case 75: return EOVERFLOW;
#endif
#ifdef EILSEQ
    case 84: return EILSEQ;
#endif
#ifdef EOPNOTSUPP
    case 95: return EOPNOTSUPP;
#endif
#ifdef ETIMEDOUT
    case 110: return ETIMEDOUT;
#endif
#ifdef EALREADY
    case 114: return EALREADY;
#endif
#ifdef EINPROGRESS
    case 115: return EINPROGRESS;
#endif
#ifdef ECANCELED
    case 125: return ECANCELED;
#endif
    default: return EIO;
    }
}

/* ---- V3DM$Stats: what the GPU module was asked, and the time it took,
   on stderr every 5 seconds (for finding where a program's frames go) ---- */

#define HAL_CounterRate   19
#define HAL_CounterPeriod 20
#define HAL_CounterRead   21
#ifndef OS_Hardware
#define OS_Hardware 0x7A
#endif

static unsigned hal_rate, hal_period;

static int hal_counter(int entry, unsigned *out)
{
    return !_swix(OS_Hardware, _INR(0, 0) | _IN(8) | _IN(9) | _OUT(0), 0, 0, entry, out);
}

/* seconds, to the HAL counter's precision where it has one */
static double stats_now(void)
{
    unsigned cs1 = 0, cs2 = 0, cnt = 0;
    if (!hal_rate && !(hal_counter(HAL_CounterRate, &hal_rate) && hal_counter(HAL_CounterPeriod, &hal_period) &&
                       hal_rate >= 10000 && hal_period))
        hal_rate = 1;                       /* centiseconds only */
    do {
        _swix(OS_ReadMonotonicTime, _OUT(0), &cs1);
        if (hal_rate == 1 || !hal_counter(HAL_CounterRead, &cnt))
            return cs1 / 100.0;
        _swix(OS_ReadMonotonicTime, _OUT(0), &cs2);
    } while (cs1 != cs2);
    if (cnt > hal_period)
        cnt = hal_period;
    return cs1 / 100.0 + (double) (hal_period - cnt) / hal_rate;
}

static struct { unsigned long request; unsigned n, errors; double secs; uint64_t bytes; } st[48];
static double st_start;
static struct drm_riscos_file_stats gpu_last;   /* the module's figures at the last line */
static int gpu_stats = 1;                       /* 0: the module has no FILE_STATS */

/* The GPU's own time for this file since the last line (V3DRender 0.13:
   time from a job put on the GPU to its interrupt; bin and render
   overlap) */
static void stats_gpu(int fd)
{
    static const char *const queue[5] = { "bin", "render", "TFU", "CSD", "clean" };
    struct drm_riscos_file_stats now;
    int r = -1, q;
    if (!gpu_stats || fake || !device.ioctl_swi)
        return;
    memset(&now, 0, sizeof now);
    if (_swix(device.ioctl_swi, _INR(0, 2) | _OUT(0), fd, DRM_IOCTL_RISCOS_FILE_STATS, &now, &r) || r) {
        gpu_stats = 0;                  /* before V3DRender 0.13 */
        return;
    }
    fprintf(stderr, "V3DM$Stats: GPU time:");
    for (q = 0; q < 5; q++) {
        unsigned jobs = now.file[q].jobs - gpu_last.file[q].jobs;
        if (jobs)
            fprintf(stderr, " %s %u jobs %.1f ms;", queue[q], jobs,
                    (double) (now.file[q].busy_ns - gpu_last.file[q].busy_ns) / 1e6);
    }
    fputc('\n', stderr);
    gpu_last = now;
}

static const char *request_name(unsigned long request)
{
    switch (request & 0xffff) {
    case 0x6440: return "SUBMIT_CL";
    case 0x6441: return "WAIT_BO";
    case 0x6442: return "CREATE_BO";
    case 0x6443: return "MMAP_BO";
    case 0x6444: return "GET_PARAM";
    case 0x6445: return "GET_BO_OFFSET";
    case 0x6446: return "SUBMIT_TFU";
    case 0x6447: return "SUBMIT_CSD";
    case 0x6409: return "GEM_CLOSE";
    case 0x64C3: return "SYNCOBJ_WAIT";
    case 0x64C4: return "SYNCOBJ_RESET";
    case 0x5200: return "SYNC_FILE_WAIT";
    case 0x5202: return "IMPORT";
    default: return NULL;
    }
}

static void stats_note(int fd, unsigned long request, void *arg, int r, double secs)
{
    unsigned i;
    double now;
    for (i = 0; i < sizeof st / sizeof st[0] && st[i].n && st[i].request != request; i++)
        ;
    if (i < sizeof st / sizeof st[0]) {
        st[i].request = request;
        st[i].n++;
        st[i].errors += r < 0;
        st[i].secs += secs;
        if ((request & 0xffff) == 0x6442)
            st[i].bytes += ((struct drm_v3d_create_bo *) arg)->size;
    }
    now = stats_now();
    if (!st_start)
        st_start = now;
    if (now - st_start < 5)
        return;
    fprintf(stderr, "V3DM$Stats: %.1f s:", now - st_start);
    for (i = 0; i < sizeof st / sizeof st[0] && st[i].n; i++) {
        const char *name = request_name(st[i].request);
        if (name)
            fprintf(stderr, " %s %u", name, st[i].n);
        else
            fprintf(stderr, " %c&%02lX %u", (int) ((st[i].request >> 8) & 0xff), st[i].request & 0xff, st[i].n);
        if (st[i].errors)
            fprintf(stderr, " (%u refused)", st[i].errors);
        fprintf(stderr, " %.1f ms", st[i].secs * 1000);
        if (st[i].bytes)
            fprintf(stderr, " %llu KB", (unsigned long long) (st[i].bytes / 1024));
        fputc(';', stderr);
    }
    fputc('\n', stderr);
    stats_gpu(fd);
    memset(st, 0, sizeof st);
    st_start = now;
}

/* ---- memory the CPU doesn't cache ---- */

#ifndef OS_DynamicArea
#define OS_DynamicArea 0x66
#endif
#ifndef OS_ScreenMode
#define OS_ScreenMode 0x65
#endif

int drm_riscos_cpu_uncached(const void *p, size_t n)
{
    int type, area, area2, flags, cache;
    if (!n)
        return 0;
    if (_swix(OS_DynamicArea, _INR(0, 1) | _OUTR(0, 1), 20, p, &type, &area) || type != 0 ||
        _swix(OS_DynamicArea, _INR(0, 1) | _OUTR(0, 1), 20, (const char *) p + n - 1, &type, &area2) ||
        type != 0 || area2 != area)
        return 0;                       /* not all in one dynamic area */
    if (_swix(OS_DynamicArea, _INR(0, 1) | _OUT(4), 2, area, &flags) || !(flags & (1 << 5)))
        return 0;                       /* cacheable (DA flag bit 5: not cacheable) */
    /* screen memory: RISC OS 5 can cache the screen and clean it every few
       vsyncs; only when that is suspended (OS_ScreenMode 4 bit 0) */
    if (area == 2 && (_swix(OS_ScreenMode, _INR(0, 2) | _OUT(1), 4, -1, -1, &cache) || !(cache & 1)))
        return 0;
    return 1;
}

/* ---- memory with pages behind it ----
   The GPU module maps memory by its physical pages when it is imported. A
   page with none behind it yet (ARMEABISupport's areas, which put a page in
   when it is first touched: UnixLib's shared memory, 21 Nov 2026)
   went to the GPU as page 0, and the GPU's writes went somewhere else.
   Each page is looked up (OS_Memory 0, logical to physical); one with no
   page is read once, so that an area that pages in on touch does so, and
   looked up again. */

#ifndef OS_Memory
#define OS_Memory 0x68
#endif

static int page_present(uintptr_t a)
{
    unsigned block[3] = { 0, (unsigned) a, 0 };
    return !_swix(OS_Memory, _INR(0, 2), (1 << 9) | (1 << 13), block, 1) && block[2] != 0;
}

int drm_riscos_memory_present(const void *p, size_t n, uintptr_t *missing)
{
    uintptr_t a = (uintptr_t) p & ~(uintptr_t) 4095, end = (uintptr_t) p + n;
    modes();
    if (fake)
        return 1;                       /* (nothing is mapped into a GPU) */
    for (; a < end; a += 4096) {
        if (page_present(a))
            continue;
        (void) *(volatile const unsigned char *) a;     /* paged in on touch */
        if (!page_present(a)) {
            if (missing)
                *missing = a;
            return 0;
        }
    }
    return 1;
}

/* ---- the call ---- */

int drmIoctl(int fd, unsigned long request, void *arg)
{
    int r = 0;
    _kernel_oserror *e = NULL;
    double t0 = 0;
    if (closed_at_exit)
        return 0;                               /* (exiting: see close_at_exit) */
    modes();
    drm_riscos_dump_before(request, arg);
    if (stats)
        t0 = stats_now();
    if (fake)
        r = drm_riscos_fake_ioctl(request, arg);
    else if (!device.ioctl_swi)
        r = -ENODEV;
    else
        e = _swix(device.ioctl_swi, _INR(0, 2) | _OUT(0), fd, request, arg, &r);
    if (!e)
        drm_riscos_dump_after(request, arg, r);
    if (stats)
        stats_note(fd, request, arg, e ? -1 : r, stats_now() - t0);
    if (trace) {
        fprintf(stderr, "drm: file %d request %08lx (type %c nr &%02lX size %lu) -> %d%s\n", fd, request,
                (int) ((request >> 8) & 0xff), request & 0xff, (request >> 16) & 0x3fff, r,
                e ? " (SWI error)" : "");
        if (request == DRM_IOCTL_RISCOS_IMPORT_MEMORY) {
            const struct drm_riscos_import_memory *im = arg;
            fprintf(stderr, "drm:   import %08x size %u%s -> handle %u, GPU address %08x\n",
                    (unsigned) im->address, im->size,
                    im->flags & DRM_RISCOS_IMPORT_UNCACHED ? " (uncached)" : "", im->handle, im->offset);
        }
    }
    if (e) {
        errno = EBADF;
        return -1;
    }
    if (r < 0) {
        errno = from_linux_errno(-r);           /* (the stand-in answers in Linux's too) */
        return -1;
    }
    return 0;
}

int drmCommandWriteRead(int fd, unsigned long index, void *data, unsigned long size)
{
    unsigned long request = DRM_IOC(DRM_IOC_READ | DRM_IOC_WRITE, DRM_IOCTL_BASE,
                                    DRM_COMMAND_BASE + index, size);
    return drmIoctl(fd, request, data) ? -errno : 0;
}

int drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
    struct drm_get_cap cap;
    memset(&cap, 0, sizeof cap);
    cap.capability = capability;
    if (drmIoctl(fd, DRM_IOCTL_GET_CAP, &cap))
        return -errno;
    *value = cap.value;
    return 0;
}

/* The module's DRM_IOCTL_VERSION (as libdrm asks it: the lengths first,
   then the strings); if it doesn't answer, version 1.0 of the device's
   name. (Some of Mesa's drivers choose features by the minor version.) */
static char *version_string(char *p, size_t len)
{
    char *s = malloc(len + 1);
    if (s) {
        memcpy(s, p ? p : "", p ? len : 0);
        s[p ? len : 0] = 0;
    }
    return s;
}

drmVersionPtr drmGetVersion(int fd)
{
    struct drm_version q;
    drmVersionPtr v = calloc(1, sizeof *v);
    char *name = NULL, *date = NULL, *desc = NULL;
    if (!v)
        return NULL;
    find_device();
    memset(&q, 0, sizeof q);
    if (drmIoctl(fd, DRM_IOCTL_VERSION, &q) == 0 && q.name_len > 0 && q.name_len < 64 &&
        q.date_len < 64 && q.desc_len < 256) {
        q.name = name = calloc(1, q.name_len + 1);
        q.date = date = calloc(1, q.date_len + 1);
        q.desc = desc = calloc(1, q.desc_len + 1);
        if (name && date && desc && drmIoctl(fd, DRM_IOCTL_VERSION, &q) == 0) {
            v->version_major = q.version_major;
            v->version_minor = q.version_minor;
            v->version_patchlevel = q.version_patchlevel;
            v->name = version_string(name, q.name_len);
            v->date = version_string(date, q.date_len);
            v->desc = version_string(desc, q.desc_len);
        }
        free(name);
        free(date);
        free(desc);
    }
    if (!v->name) {
        free(v->date);
        free(v->desc);
        v->version_major = 1;
        v->version_minor = v->version_patchlevel = 0;
        v->name = version_string(device.name, strlen(device.name));
        v->date = version_string("20260515", 8);
        v->desc = version_string("RISC OS", 7);
    }
    if (!v->name || !v->date || !v->desc) {
        drmFreeVersion(v);
        return NULL;
    }
    v->name_len = (int) strlen(v->name);
    v->date_len = (int) strlen(v->date);
    v->desc_len = (int) strlen(v->desc);
    return v;
}

void drmFreeVersion(drmVersionPtr v)
{
    if (!v)
        return;
    free(v->name);
    free(v->date);
    free(v->desc);
    free(v);
}

/* ---- sync objects ---- */

int drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
    struct drm_syncobj_create a;
    memset(&a, 0, sizeof a);
    a.flags = flags;
    if (drmIoctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &a))
        return -errno;
    *handle = a.handle;
    return 0;
}

int drmSyncobjDestroy(int fd, uint32_t handle)
{
    struct drm_syncobj_destroy a;
    memset(&a, 0, sizeof a);
    a.handle = handle;
    return drmIoctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &a) ? -errno : 0;
}

int drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd)
{
    (void) fd; (void) handle; (void) obj_fd;
    return -EINVAL;         /* sync objects can't be shared between programs */
}

int drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle)
{
    (void) fd; (void) obj_fd; (void) handle;
    return -EINVAL;
}

int drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd)
{
    struct drm_syncobj_handle a;
    memset(&a, 0, sizeof a);
    a.handle = handle;
    a.fd = sync_file_fd;
    a.flags = DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE;
    return drmIoctl(fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &a) ? -errno : 0;
}

int drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd)
{
    struct drm_syncobj_handle a;
    memset(&a, 0, sizeof a);
    a.handle = handle;
    a.fd = -1;
    a.flags = DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE;
    if (drmIoctl(fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &a))
        return -errno;
    *sync_file_fd = a.fd;
    return 0;
}

/* Mesa's deadline (CLOCK_MONOTONIC, ns) on the modules' clock */
static int64_t to_module_clock(int64_t deadline)
{
    struct timespec ts;
    unsigned int cs = 0;
    int64_t mono_now, ours_now;
    if (deadline == INT64_MAX || deadline < 0)
        return deadline < 0 ? 0 : INT64_MAX;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    mono_now = (int64_t) ts.tv_sec * 1000000000 + ts.tv_nsec;
    _swix(OS_ReadMonotonicTime, _OUT(0), &cs);
    ours_now = (int64_t) cs * 10000000;
    if (deadline <= mono_now)
        return ours_now;
    if (deadline - mono_now > INT64_MAX - ours_now)
        return INT64_MAX;
    return ours_now + (deadline - mono_now);
}

int drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles, int64_t timeout_nsec,
                   unsigned flags, uint32_t *first_signaled)
{
    struct drm_syncobj_wait a;
    memset(&a, 0, sizeof a);
    a.handles = (uintptr_t) handles;
    a.timeout_nsec = to_module_clock(timeout_nsec);
    a.count_handles = num_handles;
    a.flags = flags;
    if (drmIoctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &a))
        return -errno;
    if (first_signaled)
        *first_signaled = a.first_signaled;
    return 0;
}

static int syncobj_array(int fd, unsigned long request, const uint32_t *handles, uint32_t count)
{
    struct drm_syncobj_array a;
    memset(&a, 0, sizeof a);
    a.handles = (uintptr_t) handles;
    a.count_handles = count;
    return drmIoctl(fd, request, &a) ? -errno : 0;
}

int drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count)
{
    return syncobj_array(fd, DRM_IOCTL_SYNCOBJ_RESET, handles, count);
}

int drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t count)
{
    return syncobj_array(fd, DRM_IOCTL_SYNCOBJ_SIGNAL, handles, count);
}

/* timeline sync objects: not offered (Mesa's v3d gallium driver doesn't
   use them) */
int drmSyncobjTimelineSignal(int fd, const uint32_t *handles, uint64_t *points, uint32_t count)
{
    (void) fd; (void) handles; (void) points; (void) count;
    return -ENOSYS;
}

int drmSyncobjTimelineWait(int fd, uint32_t *handles, uint64_t *points, unsigned num_handles,
                           int64_t timeout_nsec, unsigned flags, uint32_t *first_signaled)
{
    (void) fd; (void) handles; (void) points; (void) num_handles;
    (void) timeout_nsec; (void) flags; (void) first_signaled;
    return -ENOSYS;
}

int drmSyncobjQuery2(int fd, uint32_t *handles, uint64_t *points, uint32_t handle_count, uint32_t flags)
{
    (void) fd; (void) handles; (void) points; (void) handle_count; (void) flags;
    return -ENOSYS;
}

int drmSyncobjTransfer(int fd, uint32_t dst_handle, uint64_t dst_point,
                       uint32_t src_handle, uint64_t src_point, uint32_t flags)
{
    (void) fd; (void) dst_handle; (void) dst_point; (void) src_handle; (void) src_point; (void) flags;
    return -ENOSYS;
}

/* ---- PRIME (dma-buf): not on RISC OS ---- */

int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
    (void) fd; (void) handle; (void) flags; (void) prime_fd;
    errno = EINVAL;
    return -1;
}

int drmPrimeFDToHandle(int fd, int prime_fd, uint32_t *handle)
{
    (void) fd; (void) prime_fd; (void) handle;
    errno = EINVAL;
    return -1;
}

/* ---- sync files (libsync.h) ----
   The module's file they belong to: the program's one file. */

#include "libsync.h"

static int riscos_sync_file(int fd, int timeout_ms, unsigned long request)
{
    struct drm_riscos_sync_file a;
    memset(&a, 0, sizeof a);
    a.fd = fd;
    a.timeout_ms = timeout_ms;
    if (program_file <= 0) {
        errno = EBADF;
        return -1;
    }
    if (drmIoctl(program_file, request, &a))
        return -1;
    if (request == DRM_IOCTL_RISCOS_SYNC_FILE_WAIT && !a.signalled) {
        errno = ETIME;
        return -1;
    }
    return 0;
}

int sync_wait(int fd, int timeout)
{
    return riscos_sync_file(fd, timeout, DRM_IOCTL_RISCOS_SYNC_FILE_WAIT);
}

int sync_close(int fd)
{
    return riscos_sync_file(fd, 0, DRM_IOCTL_RISCOS_SYNC_FILE_CLOSE);
}

int sync_merge(const char *name, int fd1, int fd2)
{
    (void) name; (void) fd1; (void) fd2;
    errno = EINVAL;
    return -1;
}

int sync_accumulate(const char *name, int *fd1, int fd2)
{
    (void) name; (void) fd1; (void) fd2;
    errno = EINVAL;
    return -1;
}
