#ifndef FAKE_RISCOS_H
#define FAKE_RISCOS_H

#define FAKE_MAX_WINDOWS 4

typedef struct {
    int w, h, xeig, yeig, log2bpp, flags, line_length;
    unsigned int *mem;          /* room for 3 banks; bank n at mem + (n-1) * w * h */
    int da_size, da_max;        /* screen dynamic area size and limit, bytes */
    int vdu_bank, display_bank;
} fake_screen_t;

typedef struct {
    int handle;
    int x0, y0, x1, y1;     /* visible area, OS units */
    int sx, sy;             /* scroll offsets */
} fake_window_t;

extern fake_screen_t fake_screen;
extern fake_window_t fake_windows[FAKE_MAX_WINDOWS];
extern int fake_vsyncs, fake_update_calls, fake_redraw_calls, fake_plots;
/* Wimp_ForceRedraw calls and the last rectangle (window, x0, y0, x1, y1) */
extern int fake_force_redraws, fake_force_rect[5];
extern int fake_scaled_plots;          /* OS_SpriteOp 52 */

void fake_set_screen(int w, int h, int trgb, int log2bpp);
fake_window_t *fake_open_window(int handle, int x0, int y0, int x1, int y1, int sx, int sy);
unsigned int fake_screen_pixel(int x, int y_from_top);
unsigned int fake_bank_pixel(int bank, int x, int y_from_top);
void fake_reset_clip(void);
/* Count pixels of value fake_watch_value plotted inside pixel rect
   fake_watch = {x0, y0 (from top), x1, y1} (exclusive). */
extern int fake_watch[4], fake_watch_value, fake_watch_hits;

/* A scripted Wimp task (Wimp_Initialise/CreateWindow/OpenWindow/Poll...).
   Wimp_Poll first returns one Redraw_Window_Request for the newest window,
   then fake_wimp_nulls Null_Reason_Codes (when nulls are unmasked), then
   each fake_wimp_script entry: {reason, value} with reason 8 = key
   (value = RISC OS key code), 6 = click or 2 = Open_Window_Request
   (value = width | height << 16, OS units, same top left), then
   Close_Window_Request.
   fake_wimp_hook, if set, is called before each Poll returns (a frame). */
extern int fake_wimp_nulls, fake_wimp_script[16][2], fake_wimp_script_len;
extern int fake_wimp_polls, fake_wimp_keys_passed, fake_wimp_tasks;
extern void (*fake_wimp_hook)(int reason);
extern const char *fake_wimp_title;
extern int fake_wimp_desktop;           /* Wimp_ReadSysInfo 0 reports a desktop */   /* the newest window's indirected title */

#endif
