/*
 * vc_dispmanx_types.h - riscos-mesa DispmanX compatibility (MIT licence).
 * Types and constants of the Raspberry Pi DispmanX API, written for
 * riscos-mesa with the same names and layouts as the Pi userland headers.
 */
#ifndef VC_DISPMANX_TYPES_H
#define VC_DISPMANX_TYPES_H

#include <stdint.h>
#include "interface/vctypes/vc_image_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VC_DISPMANX_VERSION 1

typedef uint32_t DISPMANX_DISPLAY_HANDLE_T;
typedef uint32_t DISPMANX_UPDATE_HANDLE_T;
typedef uint32_t DISPMANX_ELEMENT_HANDLE_T;
typedef uint32_t DISPMANX_RESOURCE_HANDLE_T;
typedef uint32_t DISPMANX_PROTECTION_T;

#define DISPMANX_NO_HANDLE 0

#define DISPMANX_PROTECTION_MAX  0x0f
#define DISPMANX_PROTECTION_NONE 0
#define DISPMANX_PROTECTION_HDCP 11

/* Displays */
#define DISPMANX_ID_MAIN_LCD    0
#define DISPMANX_ID_AUX_LCD     1
#define DISPMANX_ID_HDMI        2
#define DISPMANX_ID_SDTV        3
#define DISPMANX_ID_FORCE_LCD   4
#define DISPMANX_ID_FORCE_TV    5
#define DISPMANX_ID_FORCE_OTHER 6

typedef enum {
    DISPMANX_SUCCESS = 0,
    DISPMANX_INVALID = -1
} DISPMANX_STATUS_T;

typedef enum {
    DISPMANX_NO_ROTATE = 0,
    DISPMANX_ROTATE_90 = 1,
    DISPMANX_ROTATE_180 = 2,
    DISPMANX_ROTATE_270 = 3,
    DISPMANX_FLIP_HRIZ = 1 << 16,
    DISPMANX_FLIP_VERT = 1 << 17,
    DISPMANX_STEREOSCOPIC_INVERT = 1 << 19,
    DISPMANX_STEREOSCOPIC_NONE = 0 << 20,
    DISPMANX_STEREOSCOPIC_MONO = 1 << 20,
    DISPMANX_STEREOSCOPIC_SBS = 2 << 20,
    DISPMANX_STEREOSCOPIC_TB = 3 << 20,
    DISPMANX_STEREOSCOPIC_MASK = 15 << 20,
    DISPMANX_SNAPSHOT_NO_YUV = 1 << 24,
    DISPMANX_SNAPSHOT_NO_RGB = 1 << 25,
    DISPMANX_SNAPSHOT_FILL = 1 << 26,
    DISPMANX_SNAPSHOT_SWAP_RED_BLUE = 1 << 27,
    DISPMANX_SNAPSHOT_PACK = 1 << 28
} DISPMANX_TRANSFORM_T;

typedef enum {
    DISPMANX_FLAGS_ALPHA_FROM_SOURCE = 0,
    DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS = 1,
    DISPMANX_FLAGS_ALPHA_FIXED_NON_ZERO = 2,
    DISPMANX_FLAGS_ALPHA_FIXED_EXCEED_0X07 = 3,
    DISPMANX_FLAGS_ALPHA_PREMULT = 1 << 16,
    DISPMANX_FLAGS_ALPHA_MIX = 1 << 17,
    DISPMANX_FLAGS_ALPHA_DISCARD_LOWER_LAYERS = 1 << 18
} DISPMANX_FLAGS_ALPHA_T;

typedef struct {
    DISPMANX_FLAGS_ALPHA_T flags;
    uint32_t opacity;
    DISPMANX_RESOURCE_HANDLE_T mask;
} VC_DISPMANX_ALPHA_T;

typedef enum {
    DISPMANX_FLAGS_CLAMP_NONE = 0,
    DISPMANX_FLAGS_CLAMP_LUMA_TRANSPARENT = 1,
    DISPMANX_FLAGS_CLAMP_TRANSPARENT = 2,
    DISPMANX_FLAGS_CLAMP_REPLACE = 3
} DISPMANX_FLAGS_CLAMP_T;

typedef enum {
    DISPMANX_FLAGS_KEYMASK_OVERRIDE = 1,
    DISPMANX_FLAGS_KEYMASK_SMOOTH = 1 << 1,
    DISPMANX_FLAGS_KEYMASK_CR_INV = 1 << 2,
    DISPMANX_FLAGS_KEYMASK_CB_INV = 1 << 3,
    DISPMANX_FLAGS_KEYMASK_YY_INV = 1 << 4
} DISPMANX_FLAGS_KEYMASK_T;

typedef union {
    struct { uint8_t yy_upper, yy_lower, cr_upper, cr_lower, cb_upper, cb_lower; } yuv;
    struct { uint8_t red_upper, red_lower, blue_upper, blue_lower, green_upper, green_lower; } rgb;
} DISPMANX_CLAMP_KEYS_T;

typedef struct {
    DISPMANX_FLAGS_CLAMP_T mode;
    DISPMANX_FLAGS_KEYMASK_T key_mask;
    DISPMANX_CLAMP_KEYS_T key_value;
    uint32_t replace_value;
} DISPMANX_CLAMP_T;

typedef enum {
    VCOS_DISPLAY_INPUT_FORMAT_INVALID = 0,
    VCOS_DISPLAY_INPUT_FORMAT_RGB888,
    VCOS_DISPLAY_INPUT_FORMAT_RGB565
} DISPLAY_INPUT_FORMAT_T;

typedef struct {
    int32_t width;
    int32_t height;
    DISPMANX_TRANSFORM_T transform;
    DISPLAY_INPUT_FORMAT_T input_format;
    uint32_t display_num;
} DISPMANX_MODEINFO_T;

/* vc_dispmanx_element_change_attributes change_flags */
#define ELEMENT_CHANGE_LAYER          (1 << 0)
#define ELEMENT_CHANGE_OPACITY        (1 << 1)
#define ELEMENT_CHANGE_DEST_RECT      (1 << 2)
#define ELEMENT_CHANGE_SRC_RECT       (1 << 3)
#define ELEMENT_CHANGE_MASK_RESOURCE  (1 << 4)
#define ELEMENT_CHANGE_TRANSFORM      (1 << 5)

typedef void (*DISPMANX_CALLBACK_FUNC_T)(DISPMANX_UPDATE_HANDLE_T u, void *arg);
typedef void (*DISPMANX_PROGRESS_CALLBACK_FUNC_T)(DISPMANX_UPDATE_HANDLE_T u,
                                                  uint32_t line, void *arg);

#ifdef __cplusplus
}
#endif

#endif
