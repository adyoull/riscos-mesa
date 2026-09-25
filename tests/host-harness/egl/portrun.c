/*
 * portrun.c - runs a ported program (a Wimp task with an EGL window) on
 * the fake RISC OS, for checking ports on a Linux host (docs/porting).
 * Build the program with -Dmain=app_main against fake/, egl_riscos.c and
 * a host libOSMesa; see README.md.
 *
 * Environment: FRAMES = null events to deliver (frames, for animating
 * programs); KEYS = a comma-separated script delivered after them, each
 * entry followed by a null event:
 *   <n>          key press, RISC OS key code n (0x... for hex)
 *   r<w>x<h>     resize the window (OS units)
 *   c<x>:<y>[:<b>]  click at pixel x, y of the window (buttons b: 4 Select
 *                (default), 2 Menu, 1 Adjust), held until "u"
 *   m<x>:<y>     move the pointer there;  u  release the buttons
 *   s<i>[:<j>[:<k>]]  choose from the open menu (item indices)
 *   w<d>         turn the scroll wheel by d;  n  one more null event
 * PPM = where to save the fake 1280x720 screen when the close request
 * comes; MENUS=1 prints each menu opened.
 * The fake reports a running desktop (Wimp_ReadSysInfo), so DispmanX
 * programs use libbcm_host's window mode unless <App>$Display says "full".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "fake_riscos.h"
int app_main(int argc, char **argv);
static struct timespec t0;
static void dump(void) {
  struct timespec b; FILE *f = fopen(getenv("PPM"), "wb"); int x, y;
  clock_gettime(CLOCK_MONOTONIC, &b);
  fprintf(stderr, "host: %d polls in %.2f s; plots %d, redraws %d, updates %d, windows open %d\n", fake_wimp_polls,
    (b.tv_sec-t0.tv_sec)+(b.tv_nsec-t0.tv_nsec)/1e9, fake_plots, fake_redraw_calls, fake_update_calls, fake_windows[0].handle != 0); fprintf(stderr, "host: title \"%s\"\n", fake_wimp_title ? fake_wimp_title : "");
  fprintf(f, "P6 1280 720 255\n");
  for (y = 0; y < 720; y++) for (x = 0; x < 1280; x++) { unsigned p = fake_screen_pixel(x, y); fputc(p & 255, f); fputc((p >> 8) & 255, f); fputc((p >> 16) & 255, f); }
  fclose(f);
}
static void hook(int reason) { if (reason == 3) dump(); }
static int ac; static char **av;
static void *run(void *x) {
  char *k = getenv("KEYS");
  fake_set_screen(1280, 720, 0, 5); fake_reset_clip();
  fake_wimp_nulls = atoi(getenv("FRAMES")); fake_wimp_hook = hook;
  fake_wimp_desktop = 1;             /* Wimp_ReadSysInfo reports a desktop */
  fake_log_menus = getenv("MENUS") != NULL;
  while (k && *k) {
    int *e = fake_wimp_script[fake_wimp_script_len++], i;
    e[1] = e[2] = e[3] = -1;
    switch (*k) {
    case 'r': { int w = strtol(k + 1, &k, 0), h = strtol(k + 1, &k, 0); e[0] = 2; e[1] = w | (h << 16); break; }
    case 'c': e[0] = 6; e[3] = 0; goto numbers;
    case 'm': e[0] = FAKE_MOVE; goto numbers;
    case 's': e[0] = 9; goto numbers;
    case 'w': e[0] = FAKE_WHEEL; goto numbers;
    case 'u': e[0] = FAKE_RELEASE; k++; break;
    case 'n': e[0] = FAKE_NULL; k++; break;
    default:  e[0] = 8; e[1] = strtol(k, &k, 0); break;
    numbers:
      k++;
      for (i = 1; i <= 3; i++) { e[i] = strtol(k, &k, 0); if (*k != ':') break; k++; }
      if (e[0] == 6 && e[3] <= 0) e[3] = 4;
      break;
    }
    if (*k == ',') k++; }
  clock_gettime(CLOCK_MONOTONIC, &t0);
  app_main(ac, av);
  fprintf(stderr, "host: app_main returned\n"); dump();
  return 0; }
int main(int argc, char **argv) { pthread_attr_t a; pthread_t t; void *st; ac = argc; av = argv;
  mallopt(M_ARENA_MAX,1); mallopt(M_MMAP_MAX,0); mallopt(M_TOP_PAD, 64<<20);
  st = mmap((void*)0x60000000, 8<<20, 3, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
  pthread_attr_init(&a); pthread_attr_setstack(&a, st, 8<<20); pthread_create(&t, &a, run, 0); pthread_join(t, 0); fflush(stdout); _exit(0); }
