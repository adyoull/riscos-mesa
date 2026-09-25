/*
 * vc_dispmanx.h - riscos-mesa DispmanX compatibility (MIT licence).
 *
 * The DispmanX calls that EGL programs written for the Raspberry Pi's
 * Khronos stack use to get a window. On RISC OS an element is a rectangle
 * of the screen: its EGL surface is plotted there (scaled from the source
 * rectangle to the destination rectangle) on each eglSwapBuffers, and the
 * desktop underneath is redrawn when the element goes. Layers, opacity,
 * transforms and resources (2D images) aren't supported.
 */
#ifndef VC_DISPMANX_H
#define VC_DISPMANX_H

#include "interface/vmcs_host/vc_dispmanx_types.h"

#ifdef __cplusplus
extern "C" {
#endif

int vc_dispmanx_rect_set(VC_RECT_T *rect, uint32_t x_offset, uint32_t y_offset,
                         uint32_t width, uint32_t height);

DISPMANX_DISPLAY_HANDLE_T vc_dispmanx_display_open(uint32_t device);
DISPMANX_DISPLAY_HANDLE_T vc_dispmanx_display_open_mode(uint32_t device, uint32_t mode);
int vc_dispmanx_display_close(DISPMANX_DISPLAY_HANDLE_T display);
int vc_dispmanx_display_get_info(DISPMANX_DISPLAY_HANDLE_T display,
                                 DISPMANX_MODEINFO_T *pinfo);
int vc_dispmanx_display_set_background(DISPMANX_UPDATE_HANDLE_T update,
                                       DISPMANX_DISPLAY_HANDLE_T display,
                                       uint8_t red, uint8_t green, uint8_t blue);

DISPMANX_UPDATE_HANDLE_T vc_dispmanx_update_start(int32_t priority);
int vc_dispmanx_update_submit(DISPMANX_UPDATE_HANDLE_T update,
                              DISPMANX_CALLBACK_FUNC_T cb_func, void *cb_arg);
int vc_dispmanx_update_submit_sync(DISPMANX_UPDATE_HANDLE_T update);

DISPMANX_ELEMENT_HANDLE_T vc_dispmanx_element_add(DISPMANX_UPDATE_HANDLE_T update,
        DISPMANX_DISPLAY_HANDLE_T display, int32_t layer, const VC_RECT_T *dest_rect,
        DISPMANX_RESOURCE_HANDLE_T src, const VC_RECT_T *src_rect,
        DISPMANX_PROTECTION_T protection, VC_DISPMANX_ALPHA_T *alpha,
        DISPMANX_CLAMP_T *clamp, DISPMANX_TRANSFORM_T transform);
int vc_dispmanx_element_change_attributes(DISPMANX_UPDATE_HANDLE_T update,
        DISPMANX_ELEMENT_HANDLE_T element, uint32_t change_flags, int32_t layer,
        uint8_t opacity, const VC_RECT_T *dest_rect, const VC_RECT_T *src_rect,
        DISPMANX_RESOURCE_HANDLE_T mask, DISPMANX_TRANSFORM_T transform);
int vc_dispmanx_element_change_layer(DISPMANX_UPDATE_HANDLE_T update,
                                     DISPMANX_ELEMENT_HANDLE_T element, int32_t layer);
int vc_dispmanx_element_remove(DISPMANX_UPDATE_HANDLE_T update,
                               DISPMANX_ELEMENT_HANDLE_T element);

/* Not available on RISC OS: always return -1. */
int vc_dispmanx_vsync_callback(DISPMANX_DISPLAY_HANDLE_T display,
                               DISPMANX_CALLBACK_FUNC_T cb_func, void *cb_arg);

#ifdef __cplusplus
}
#endif

#endif
