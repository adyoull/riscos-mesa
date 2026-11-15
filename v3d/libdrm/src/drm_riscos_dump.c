/*
 * drm_riscos_dump.c - V3DM$Dump: the first jobs Mesa sends, with every
 * buffer they use, written to a file, so that they can be replayed or
 * compared on the build machine (V3DRender's simulated V3D) without the
 * Pi.
 *
 *   *Set V3DM$Dump <file>         (V3DM$DumpJobs: how many, default 3)
 *
 * The libdrm layer notes each buffer's size and GPU address (CREATE_BO)
 * and where the program sees it (MMAP_BO); at each SUBMIT_CL it writes,
 * before the request goes to V3DRender:
 *
 *   "V3DMDMP1"  u32 job number (from 0)
 *   u32 size of the submit structure, then the structure (drm_v3d_submit_cl)
 *   u32 number of buffers, then for each one in the job's list:
 *     u32 handle, u32 GPU address, u32 size, u32 1 if its contents follow
 *     (0 if the program never mapped it), the contents (size bytes)
 *
 * all little-endian, one record per job, appended. tools/v3dm-dump.py
 * prints them.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xf86drm.h"
#include "drm-uapi/v3d_drm.h"
#include "drm_riscos.h"
#include "drm_riscos_dump.h"

#define MAX_BOS 4096
static struct { uint32_t size, gpu; void *cpu; } bos[MAX_BOS];
static char *file;
static int jobs_left = -1, job_no;

static void start(void)
{
    const char *f, *n;
    if (jobs_left >= 0)
        return;
    jobs_left = 0;
    f = getenv("V3DM$Dump");
    if (!f || !*f)
        return;
    file = strdup(f);                /* (getenv's buffer is reused) */
    n = getenv("V3DM$DumpJobs");
    jobs_left = n && atoi(n) > 0 ? atoi(n) : 3;
    if (file) {
        FILE *o = fopen(file, "wb");    /* a new file for each run */
        if (o)
            fclose(o);
    }
}

static void u32(FILE *o, uint32_t v)
{
    unsigned char b[4] = { (unsigned char) v, (unsigned char) (v >> 8),
                           (unsigned char) (v >> 16), (unsigned char) (v >> 24) };
    fwrite(b, 1, 4, o);
}

void drm_riscos_dump_after(unsigned long request, void *arg, int result)
{
    unsigned n = (unsigned) (request & 0xff);
    start();
    if (!file || result != 0 || ((request >> 8) & 0xff) != DRM_IOCTL_BASE)
        return;
    if (n == DRM_COMMAND_BASE + DRM_V3D_CREATE_BO) {
        struct drm_v3d_create_bo *a = arg;
        if (a->handle < MAX_BOS) {
            bos[a->handle].size = a->size;
            bos[a->handle].gpu = a->offset;
            bos[a->handle].cpu = NULL;
        }
    } else if (n == DRM_COMMAND_BASE + DRM_V3D_MMAP_BO) {
        struct drm_v3d_mmap_bo *a = arg;
        if (a->handle < MAX_BOS)
            bos[a->handle].cpu = (void *) (uintptr_t) a->offset;
    } else if (n == 0x09) {             /* GEM_CLOSE */
        struct drm_gem_close *a = arg;
        if (a->handle < MAX_BOS)
            memset(&bos[a->handle], 0, sizeof bos[0]);
    }
}

void drm_riscos_dump_before(unsigned long request, void *arg)
{
    struct drm_v3d_submit_cl *s = arg;
    const uint32_t *h;
    FILE *o;
    uint32_t i;
    start();
    if (!file || jobs_left <= 0 || ((request >> 8) & 0xff) != DRM_IOCTL_BASE ||
        (request & 0xff) != DRM_COMMAND_BASE + DRM_V3D_SUBMIT_CL)
        return;
    o = fopen(file, "ab");
    if (!o)
        return;
    jobs_left--;
    fwrite("V3DMDMP1", 1, 8, o);
    u32(o, (uint32_t) job_no++);
    u32(o, (uint32_t) sizeof *s);
    fwrite(s, 1, sizeof *s, o);
    h = (const uint32_t *) (uintptr_t) s->bo_handles;
    u32(o, s->bo_handle_count);
    for (i = 0; i < s->bo_handle_count; i++) {
        uint32_t b = h[i];
        int known = b < MAX_BOS && bos[b].size;
        u32(o, b);
        u32(o, known ? bos[b].gpu : 0);
        u32(o, known ? bos[b].size : 0);
        u32(o, known && bos[b].cpu ? 1 : 0);
        if (known && bos[b].cpu)
            fwrite(bos[b].cpu, 1, bos[b].size, o);
    }
    fclose(o);
}
