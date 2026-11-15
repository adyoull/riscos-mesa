/*
 * drm_riscos_fake.c - a stand-in for V3DRender, for running Mesa's v3d
 * driver where there is no V3D (RISC OS on Linux under QEMU, or a Pi
 * without V3DRender): set the system variable V3DM$Fake (to anything).
 *
 * Everything Mesa does on the ARM then runs as on a Pi: its start-up, the
 * shader compiler, the command lists it builds, its buffers. The GPU
 * never runs, so nothing is drawn (frames read back hold what the CPU
 * last wrote there): the point is to find faults in that ARM side before
 * the Pi, not to check pictures.
 *
 * The identity answered is a Pi 4's V3D 4.2 (2 slices of 4 QPUs, rev 14).
 * Buffers are malloc'd (4 KB aligned); their "GPU address" is their ARM
 * address. Jobs are accepted and finish at once; sync objects are always
 * signalled.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "xf86drm.h"
#include "drm-uapi/v3d_drm.h"
#include "drm_riscos.h"
#include "drm_riscos_fake.h"

#define MAX_BOS 4096
static struct { void *p; uint32_t size; } bos[MAX_BOS];
static uint32_t syncobjs;

static int nr(unsigned long request) { return (int) (request & 0xff); }

static int get_param(struct drm_v3d_get_param *a)
{
    switch (a->param) {
    case DRM_V3D_PARAM_V3D_CORE0_IDENT0: a->value = 0x04443356; break;   /* "V3D", 4 */
    case DRM_V3D_PARAM_V3D_CORE0_IDENT1: a->value = 0x40000422; break;   /* .2, 2 slices x 4 QPUs, VPM 4 x 8 KB */
    case DRM_V3D_PARAM_V3D_CORE0_IDENT2: a->value = 0; break;
    case DRM_V3D_PARAM_V3D_HUB_IDENT1:   a->value = 0; break;
    case DRM_V3D_PARAM_V3D_HUB_IDENT2:   a->value = 0; break;
    case DRM_V3D_PARAM_V3D_HUB_IDENT3:   a->value = 14 << 8; break;      /* rev 14 */
    case DRM_V3D_PARAM_V3D_UIFCFG:       a->value = 0; break;
    case DRM_V3D_PARAM_SUPPORTS_TFU:
    case DRM_V3D_PARAM_SUPPORTS_CSD:
    case DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH: a->value = 1; break;
    case DRM_V3D_PARAM_GLOBAL_RESET_COUNTER:
    case DRM_V3D_PARAM_CONTEXT_RESET_COUNTER: a->value = 0; break;
    default: return -EINVAL;                /* perfmon, multisync, CPU queue, super pages */
    }
    return 0;
}

int drm_riscos_fake_ioctl(unsigned long request, void *arg)
{
    if (((request >> 8) & 0xff) != DRM_IOCTL_BASE)
        return 0;                           /* RISC OS sync files: nothing to wait for */
    switch (nr(request)) {
    case DRM_COMMAND_BASE + DRM_V3D_GET_PARAM:
        return get_param(arg);
    case DRM_COMMAND_BASE + DRM_V3D_CREATE_BO: {
        struct drm_v3d_create_bo *a = arg;
        uint32_t h;
        for (h = 1; h < MAX_BOS && bos[h].p; h++)
            ;
        if (h == MAX_BOS)
            return -ENOMEM;
        if (posix_memalign(&bos[h].p, 4096, (a->size + 4095) & ~4095u))
            bos[h].p = NULL;
        if (!bos[h].p)
            return -ENOMEM;
        memset(bos[h].p, 0, a->size);
        bos[h].size = a->size;
        a->handle = h;
        a->offset = (uint32_t) (uintptr_t) bos[h].p;
        return 0;
    }
    case DRM_COMMAND_BASE + DRM_V3D_MMAP_BO: {
        struct drm_v3d_mmap_bo *a = arg;
        if (a->handle >= MAX_BOS || !bos[a->handle].p)
            return -ENOENT;
        a->offset = (uintptr_t) bos[a->handle].p;
        return 0;
    }
    case DRM_COMMAND_BASE + DRM_V3D_GET_BO_OFFSET: {
        struct drm_v3d_get_bo_offset *a = arg;
        if (a->handle >= MAX_BOS || !bos[a->handle].p)
            return -ENOENT;
        a->offset = (uint32_t) (uintptr_t) bos[a->handle].p;
        return 0;
    }
    case DRM_COMMAND_BASE + DRM_V3D_WAIT_BO:
    case DRM_COMMAND_BASE + DRM_V3D_SUBMIT_CL:
    case DRM_COMMAND_BASE + DRM_V3D_SUBMIT_TFU:
    case DRM_COMMAND_BASE + DRM_V3D_SUBMIT_CSD:
        return 0;
    case 0x09: {                            /* GEM_CLOSE */
        struct drm_gem_close *a = arg;
        if (a->handle < MAX_BOS && bos[a->handle].p) {
            free(bos[a->handle].p);
            bos[a->handle].p = NULL;
        }
        return 0;
    }
    case 0x0c: {                            /* GET_CAP */
        struct drm_get_cap *a = arg;
        a->value = a->capability == DRM_CAP_SYNCOBJ;
        return 0;
    }
    case 0xBF: {                            /* SYNCOBJ_CREATE */
        struct drm_syncobj_create *a = arg;
        a->handle = ++syncobjs;
        return 0;
    }
    case 0xC1: {                            /* SYNCOBJ_HANDLE_TO_FD: a sync file, always signalled */
        struct drm_syncobj_handle *a = arg;
        a->fd = 1;
        return 0;
    }
    default:                                /* other sync object requests: done */
        return 0;
    }
}
