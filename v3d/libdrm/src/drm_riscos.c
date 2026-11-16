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
#include "drm_riscos.h"
#include "drm_riscos_fake.h"
#include "drm_riscos_dump.h"

#define RISC3D_Info     0xC3D00
#define RISC3D_Open     0xC3D02
#define RISC3D_Close    0xC3D03
#ifndef OS_ReadMonotonicTime
#define OS_ReadMonotonicTime 0x42
#endif

/* ---- V3DM$Fake (no V3D: drm_riscos_fake.c) and V3DM$Trace (each
   request on stderr), read once ---- */

static int fake = -1, trace;

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

int drm_riscos_open(void)
{
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

/* ---- the call ---- */

int drmIoctl(int fd, unsigned long request, void *arg)
{
    int r = 0;
    _kernel_oserror *e = NULL;
    modes();
    drm_riscos_dump_before(request, arg);
    if (fake)
        r = drm_riscos_fake_ioctl(request, arg);
    else if (!device.ioctl_swi)
        r = -ENODEV;
    else
        e = _swix(device.ioctl_swi, _INR(0, 2) | _OUT(0), fd, request, arg, &r);
    if (!e)
        drm_riscos_dump_after(request, arg, r);
    if (trace) {
        fprintf(stderr, "drm: file %d request %08lx (type %c nr &%02lX size %lu) -> %d%s\n", fd, request,
                (int) ((request >> 8) & 0xff), request & 0xff, (request >> 16) & 0x3fff, r,
                e ? " (SWI error)" : "");
        if (request == DRM_IOCTL_RISCOS_IMPORT_MEMORY) {
            const struct drm_riscos_import_memory *im = arg;
            fprintf(stderr, "drm:   import %08x size %u -> handle %u, GPU address %08x\n",
                    (unsigned) im->address, im->size, im->handle, im->offset);
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
