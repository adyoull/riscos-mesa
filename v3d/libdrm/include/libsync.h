/*
 * libsync.h for riscos-mesa's libdrm layer (v3d/libdrm): sync files on
 * RISC OS are V3DRender's numbers, not file descriptors, so waiting for
 * one is a V3DRender request (DRM_IOCTL_RISCOS_SYNC_FILE_WAIT) on the
 * program's V3DRender file. Merging sync files isn't supported (V3DRender
 * has no such request): sync_merge and sync_accumulate fail with EINVAL,
 * which is why EGL_ANDROID_native_fence_sync isn't offered.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#ifndef RISCOS_MESA_LIBSYNC_H
#define RISCOS_MESA_LIBSYNC_H
#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 0 when signalled, -1 with errno ETIME (timed out) or another error;
   timeout in ms, -1 for ever */
int sync_wait(int fd, int timeout);
int sync_merge(const char *name, int fd1, int fd2);
int sync_accumulate(const char *name, int *fd1, int fd2);
/* closes a sync file (close() would close a UnixLib descriptor) */
int sync_close(int fd);

#ifdef __cplusplus
}
#endif
#endif
