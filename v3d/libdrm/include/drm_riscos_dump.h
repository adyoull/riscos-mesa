/* drm_riscos_dump.h - V3DM$Dump (src/drm_riscos_dump.c).
   Copyright (c) 2026 Andrew Youll. MIT licence. */
#ifndef DRM_RISCOS_DUMP_H
#define DRM_RISCOS_DUMP_H
/* before each request: writes a job (SUBMIT_CL) out */
void drm_riscos_dump_before(unsigned long request, void *arg);
/* after each request: notes buffers' sizes, addresses and mappings */
void drm_riscos_dump_after(unsigned long request, void *arg, int result);
#endif
