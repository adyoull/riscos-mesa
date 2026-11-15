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
   GPU address) out. The memory must stay where it is until GEM_CLOSE. */
struct drm_riscos_import_memory { uint64_t address; uint32_t size, handle, offset, pad; };
#define DRM_IOCTL_RISCOS_IMPORT_MEMORY \
  _IOC (_IOC_READ | _IOC_WRITE, DRM_RISCOS_BASE, 0x02, sizeof (struct drm_riscos_import_memory))

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
