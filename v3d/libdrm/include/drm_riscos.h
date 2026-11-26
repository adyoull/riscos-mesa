/*
 * drm_riscos.h - riscos-mesa's libdrm layer for GPU driver modules
 * that RISC3D lists as DRM devices (V3DRender on the Pi 4): the RISC OS parts
 * that Linux does with open, close and mmap (v3d/libdrm/src).
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#ifndef DRM_RISCOS_H
#define DRM_RISCOS_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/ioccom.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The RISC OS requests (V3DRender's include/drm/drm.h; any module that
   offers sync files): waiting for and closing a sync file */
struct drm_riscos_sync_file { int32_t fd; int32_t timeout_ms; uint32_t signalled, pad; };
#define DRM_RISCOS_BASE 'R'
#define DRM_IOCTL_RISCOS_SYNC_FILE_WAIT \
  _IOC (_IOC_READ | _IOC_WRITE, DRM_RISCOS_BASE, 0x00, sizeof (struct drm_riscos_sync_file))
#define DRM_IOCTL_RISCOS_SYNC_FILE_CLOSE \
  _IOC (_IOC_WRITE, DRM_RISCOS_BASE, 0x01, sizeof (struct drm_riscos_sync_file))

/* V3DRender 0.11: a BO the CPU sees cached (drm_v3d_create_bo.flags), the
   module cleaning and invalidating it around the jobs that list it */
#define DRM_RISCOS_BO_CPU_CACHED 0x80000000u
/* V3DRender 0.11: memory the program has (4 KB aligned, a multiple of
   4 KB) as a BO, the GPU's MMU mapping its pages; handle and offset (the
   GPU address) out. The memory must stay where it is until GEM_CLOSE.
   flags (0.13; "pad" before, which 0.11 and 0.12 refuse nonzero with
   EINVAL): DRM_RISCOS_IMPORT_UNCACHED, the CPU doesn't cache this memory
   (screen memory), so no cache cleaning or invalidating around jobs.
   Never for memory the CPU caches: it would read stale lines. */
struct drm_riscos_import_memory { uint64_t address; uint32_t size, handle, offset, flags; };
#define DRM_RISCOS_IMPORT_UNCACHED 1u
#define DRM_IOCTL_RISCOS_IMPORT_MEMORY \
  _IOC (_IOC_READ | _IOC_WRITE, DRM_RISCOS_BASE, 0x02, sizeof (struct drm_riscos_import_memory))

/* V3DRender 0.13: per queue (bin, render, TFU, CSD, cache clean), the
   calling file's finished jobs and their time on the GPU, and everyone's.
   Older modules: ENOTTY. */
struct drm_riscos_queue_stats { uint64_t busy_ns; uint32_t jobs, pad; };
struct drm_riscos_file_stats { struct drm_riscos_queue_stats file[5], all[5]; };
#define DRM_IOCTL_RISCOS_FILE_STATS \
  _IOC (_IOC_READ, DRM_RISCOS_BASE, 0x03, sizeof (struct drm_riscos_file_stats))

/* V3DRender 0.31: a finished frame copied by the GPU into the screen, into
   rectangles (screen pixels, top-left origin, each with its offset in the
   source; clipped to the screen and the source). The source is a BO in
   UIF tiles (uif_height_or_stride: its padded height in rows) or in rows
   (bytes a row); format a DRM fourcc (ABGR8888/XBGR8888, ARGB8888/
   XRGB8888, RGB565; alpha ignored). rect_count 0: only whether a present
   would be taken (the screen, and the source if handle isn't 0). out_sync
   is signalled when the copy is stored. Errors: ENOTTY (not offered),
   EINVAL (this screen or source can't), EBUSY (the screen is cached by
   the CPU just now), EOPNOTSUPP (VSYNC), ENOENT (no such handle). */
struct drm_riscos_present_rect { int32_t dst_x, dst_y, width, height, src_x, src_y; };
struct drm_riscos_present {
    uint32_t handle, format, flags;
    uint32_t width, height;
    uint32_t uif_height_or_stride;
    uint32_t rect_count, pad;
    uint64_t rects;                     /* struct drm_riscos_present_rect[rect_count] */
    uint32_t in_sync, out_sync;
    uint32_t scaled_width, scaled_height;   /* (0.38) with SCALE: the size it's shown at */
};
#define DRM_RISCOS_PRESENT_SRC_UIF     1u
#define DRM_RISCOS_PRESENT_VSYNC       2u
#define DRM_RISCOS_PRESENT_SRC_UIF_XOR 4u
/* (V3DRender 0.38) shown at scaled_width x scaled_height (the rectangles'
   src_x, src_y in that), sampled by the GPU at each pixel's centre:
   nearest, or with LINEAR bilinear. UIF sources (RGBA8, RGB565) up to
   4096 x 4096 only. Only in the 56-byte request; 0.31-0.37 refuse it with
   EINVAL, so a test (rect_count 0) with SCALE says whether it's offered. */
#define DRM_RISCOS_PRESENT_SCALE       8u
#define DRM_RISCOS_PRESENT_LINEAR     16u
#define DRM_RISCOS_PRESENT_MAX_RECTS   256u
/* the 48-byte request (without the scaled size: every V3DRender from
   0.31), and the 56-byte one (0.38) */
#define DRM_IOCTL_RISCOS_PRESENT \
  _IOC (_IOC_WRITE, DRM_RISCOS_BASE, 0x04, 48)
#define DRM_IOCTL_RISCOS_PRESENT_SCALED \
  _IOC (_IOC_WRITE, DRM_RISCOS_BASE, 0x04, sizeof (struct drm_riscos_present))

/* 1 if the CPU doesn't cache any of [p, p + n): all in one dynamic area
   whose flags say not cacheable (and, for screen memory, the screen cache
   suspended); 0 if it may */
int drm_riscos_cpu_uncached(const void *p, size_t n);

/* 1 if every page of [p, p + n) has a physical page behind it (a page not
   yet put in by an area that does so when touched is read once first); 0
   if not, with the first such page's address in *missing */
int drm_riscos_memory_present(const void *p, size_t n, uintptr_t *missing);

/* For V3DM$Stats: Mesa gave a buffer of size bytes from its cache
   (from_cache 1) or made a new one */
void drm_riscos_note_bo(int from_cache, uint32_t size);

/* the GPU's DRM driver name ("v3d"), if RISC3D's choice of driver is a
   DRM device; NULL: none */
const char *drm_riscos_device(void);
/* drm_riscos_device() != NULL */
int drm_riscos_available(void);
/* a file on that device (RISC3D_Open r1), or -1 with
   errno ENODEV */
int drm_riscos_open(void);
/* closes a file from drm_riscos_open (never close() it: it isn't a
   UnixLib descriptor) */
int drm_riscos_close(int fd);
/* mmap of a BO after DRM_IOCTL_V3D_MMAP_BO: the offset is its address.
   (The offset is uint64_t, not off_t: Mesa is built with
   _FILE_OFFSET_BITS=64, where UnixLib's off_t is 8 bytes, and 4 bytes
   without it; a header shared by both must not use off_t.) */
void *drm_riscos_mmap(void *addr, size_t size, int prot, int flags, int fd, uint64_t offset);
int drm_riscos_munmap(void *addr, size_t size);

#ifdef __cplusplus
}
#endif
#endif
