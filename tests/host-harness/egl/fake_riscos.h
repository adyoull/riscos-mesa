#ifndef FAKE_RISCOS_H
#define FAKE_RISCOS_H

#define FAKE_MAX_WINDOWS 4

typedef struct {
    int w, h, xeig, yeig, log2bpp, flags, line_length;
    unsigned int *mem;
} fake_screen_t;

typedef struct {
    int handle;
    int x0, y0, x1, y1;     /* visible area, OS units */
    int sx, sy;             /* scroll offsets */
} fake_window_t;

extern fake_screen_t fake_screen;
extern fake_window_t fake_windows[FAKE_MAX_WINDOWS];
extern int fake_vsyncs, fake_update_calls, fake_redraw_calls, fake_plots;

void fake_set_screen(int w, int h, int trgb, int log2bpp);
fake_window_t *fake_open_window(int handle, int x0, int y0, int x1, int y1, int sx, int sy);
unsigned int fake_screen_pixel(int x, int y_from_top);
void fake_reset_clip(void);

#endif
