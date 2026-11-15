/* drm_riscos_fake.h - the stand-in for V3DRender (src/drm_riscos_fake.c).
   Copyright (c) 2026 Andrew Youll. MIT licence. */
#ifndef DRM_RISCOS_FAKE_H
#define DRM_RISCOS_FAKE_H
/* answers one request as a V3D 4.2 would, with nothing run: 0 or -errno */
int drm_riscos_fake_ioctl(unsigned long request, void *arg);
#endif
