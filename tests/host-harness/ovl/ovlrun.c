/*
 * ovlrun.c - runs ovltest (built with -Dmain=app_main) on the fake RISC OS
 * with the fake VideoOverlay module (fake_ovl.c).
 *
 * KEYS: a comma-separated script, each entry followed by a null event:
 *   n<N>       N null events (time passing: OVLTEST_VIRTUAL_MS per poll)
 *   <c>        the key c (one character: y, n, q, 0-3, ...)
 *   r<w>x<h>   the window resized to w x h OS units
 *   M          Message_ModeChange (the fake destroys every overlay)
 *   D          a Redraw_Window_Request for the window
 *   B          a window in front of this one (Wimp_GetWindowState), or not
 * then a close request, which ends the test.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include "fake_riscos.h"

int app_main(int argc, char **argv);
void fake_ovl_init(void);
void fake_ovl_report(void);

static int ac;
static char **av;

static void add(int reason, int a)
{
    int *e;
    if (fake_wimp_script_len >= FAKE_SCRIPT_MAX) { fprintf(stderr, "script too long\n"); exit(2); }
    e = fake_wimp_script[fake_wimp_script_len++];
    e[0] = reason; e[1] = a; e[2] = e[3] = -1;
}

static void *run(void *x)
{
    const char *k = getenv("KEYS");
    (void) x;
    fake_set_screen(1280, 720, 0, 5);
    fake_reset_clip();
    fake_wimp_desktop = 1;
    fake_ovl_init();
    while (k && *k) {
        char *end;
        if (*k == 'n' && k[1] >= '0' && k[1] <= '9') {
            int n = (int) strtol(k + 1, &end, 10);
            while (n-- > 0) add(FAKE_NULL, 0);
            k = end;
        } else if (*k == 'r' && k[1] >= '0' && k[1] <= '9') {
            int w = (int) strtol(k + 1, &end, 10), h = (int) strtol(end + 1, &end, 10);
            add(2, w | (h << 16));
            k = end;
        } else if (*k == 'B') {
            add(201, 0);                        /* another window in front of ours, or not */
            k++;
        } else if (*k == 'D') {
            add(1, 0);                          /* Redraw_Window_Request */
            k++;
        } else if (*k == 'M') {
            add(17, 0x400C1);
            k++;
        } else {
            add(8, (unsigned char) *k);
            k++;
        }
        if (*k == ',') k++;
    }
    app_main(ac, av);
    fprintf(stderr, "host: app_main returned after %d polls\n", fake_wimp_polls);
    fake_ovl_report();
    return 0;
}

int main(int argc, char **argv)
{
    pthread_attr_t a;
    pthread_t t;
    void *st;
    ac = argc; av = argv;
    /* keep every pointer below 2 GB: the program passes them in ints */
    mallopt(M_ARENA_MAX, 1); mallopt(M_MMAP_MAX, 0); mallopt(M_TOP_PAD, 64 << 20);
    st = mmap((void *) 0x60000000, 8 << 20, 3, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    pthread_attr_init(&a);
    pthread_attr_setstack(&a, st, 8 << 20);
    pthread_create(&t, &a, run, 0);
    pthread_join(t, 0);
    fflush(stdout);
    _exit(0);
}
