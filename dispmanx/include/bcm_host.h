/*
 * bcm_host.h - riscos-mesa DispmanX compatibility (MIT licence).
 *
 * Lets EGL / OpenGL ES programs written for the Raspberry Pi's Khronos stack
 * (bcm_host_init, graphics_get_display_size, vc_dispmanx_*, a pointer to an
 * EGL_DISPMANX_WINDOW_T as the native window) build and run with
 * riscos-mesa's software GL. Link: -lbcm_host -lEGL -lOSMesa -lstdc++ -lz -lm
 * (empty libGLESv2, libGLESv1_CM, libvcos and libvchiq_arm are supplied so
 * existing link lines work).
 *
 * Include this before the EGL headers (as the Pi examples do), or compile
 * with -DEGL_RISCOS_DISPMANX: it makes EGLNativeWindowType a pointer.
 */
#ifndef BCM_HOST_H
#define BCM_HOST_H

#if defined(EGL_RISCOS_NATIVE_TYPES_DEFINED) && !defined(EGL_RISCOS_DISPMANX)
#error "include bcm_host.h before the EGL headers, or compile with -DEGL_RISCOS_DISPMANX"
#endif
#ifndef EGL_RISCOS_DISPMANX
#define EGL_RISCOS_DISPMANX 1
#endif

#include <stdint.h>
#include "interface/vmcs_host/vc_dispmanx.h"

#ifdef __cplusplus
extern "C" {
#endif

void bcm_host_init(void);
void bcm_host_deinit(void);

/* The current screen mode's size in pixels. Returns 0 (or -1 on failure). */
int32_t graphics_get_display_size(const uint16_t display_number,
                                  uint32_t *width, uint32_t *height);

#ifdef __cplusplus
}
#endif

/* The Pi's EGL headers declare eglSaneChooseConfigBRCM; here it comes with
   bcm_host.h (included before the EGL headers, so the types match). */
#include "EGL/eglext_brcm.h"

#endif
