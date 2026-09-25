/*
 * riscos_dispmanx.h - private interface between riscos-mesa's libEGL and
 * its DispmanX compatibility library (libbcm_host). MIT licence.
 *
 * libEGL refers to these weakly, so programs that don't link libbcm_host
 * don't need it.
 */
#ifndef RISCOS_DISPMANX_H
#define RISCOS_DISPMANX_H

typedef struct {
    int visible;            /* 0: element removed, or opacity 0 */
    int window;             /* window mode: the Wimp window the display is shown
                               in (x, y are then from its work area's top
                               left); 0 = the screen */
    int x, y, w, h;         /* destination, pixels, from the screen's top left */
    int src_x, src_y;       /* source rectangle in the surface, pixels */
    int src_w, src_h;       /* (0 = the whole surface) */
} riscos_dmx_placement;

/* If native_window points to an EGL_DISPMANX_WINDOW_T for a live element,
   set *id (the element) and *w, *h (the surface size) and return 1. */
int __riscos_dispmanx_window(const void *native_window, int *id, int *w, int *h);

/* Where element id is shown now. Returns 0 if it no longer exists. */
int __riscos_dispmanx_placement(int id, riscos_dmx_placement *p);

/* Called by libEGL after it has shown a frame of an element in window mode:
   libbcm_host polls the Wimp there, so the program multitasks. */
void __riscos_dispmanx_swapped(void);

#endif
