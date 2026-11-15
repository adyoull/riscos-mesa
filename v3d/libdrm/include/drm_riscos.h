/*
 * drm_riscos.h - riscos-mesa's libdrm layer for V3DRender: the RISC OS
 * parts that Linux does with open, close and mmap (v3d/libdrm/src).
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

/* V3DRender's RISC OS requests (its include/drm/drm.h): sync files */
struct drm_riscos_sync_file { int32_t fd; int32_t timeout_ms; uint32_t signalled, pad; };
#define DRM_RISCOS_BASE 'R'
#define DRM_IOCTL_RISCOS_SYNC_FILE_WAIT \
  _IOC (_IOC_READ | _IOC_WRITE, DRM_RISCOS_BASE, 0x00, sizeof (struct drm_riscos_sync_file))
#define DRM_IOCTL_RISCOS_SYNC_FILE_CLOSE \
  _IOC (_IOC_WRITE, DRM_RISCOS_BASE, 0x01, sizeof (struct drm_riscos_sync_file))

/* V3DRender is loaded and drives a V3D ("v3d") */
int drm_riscos_available(void);
/* a V3DRender file (V3DRender_Open), or -1 with errno ENODEV */
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
