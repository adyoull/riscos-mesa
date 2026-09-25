/*
 * eglext_brcm.h - riscos-mesa DispmanX compatibility (MIT licence).
 *
 * The one Broadcom EGL extension function Raspberry Pi example code
 * commonly calls. It is in libbcm_host (the porting aid), not libEGL.
 */
#ifndef EGLEXT_BRCM_H
#define EGLEXT_BRCM_H

#include <EGL/egl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* eglChooseConfig, but a "closest match" instead of failing: on the Pi
   it relaxes attributes the hardware can't meet. Here the multisample
   attributes (EGL_SAMPLES, EGL_SAMPLE_BUFFERS) are dropped, as
   riscos-mesa has no multisample configs; everything else is passed to
   eglChooseConfig unchanged. */
EGLBoolean eglSaneChooseConfigBRCM(EGLDisplay dpy, const EGLint *attrib_list,
                                   EGLConfig *configs, EGLint config_size,
                                   EGLint *num_config);

#ifdef __cplusplus
}
#endif

#endif
