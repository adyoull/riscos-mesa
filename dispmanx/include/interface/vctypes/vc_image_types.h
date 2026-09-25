/*
 * vc_image_types.h - riscos-mesa DispmanX compatibility (MIT licence).
 * Only what the DispmanX calls need. Written for riscos-mesa; the names and
 * layouts match the Raspberry Pi userland headers so existing code builds.
 */
#ifndef VC_IMAGE_TYPES_H
#define VC_IMAGE_TYPES_H

#include <stdint.h>

typedef struct tag_VC_RECT_T {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} VC_RECT_T;

#endif
