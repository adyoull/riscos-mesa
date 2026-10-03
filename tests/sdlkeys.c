/*
 * sdlkeys - checks SDL's key events on RISC OS: one SDL_KEYDOWN per press,
 * repeats (event.key.repeat = 1) only while a key is held, after the
 * keyboard's auto-repeat delay and at its rate (*Configure Delay/Repeat,
 * *FX 11/12; none after *FX 11,0), and one SDL_KEYUP.
 *
 * Opens a small window (a desktop window when run from the desktop). The
 * title shows the count of presses, repeats and releases and the last key;
 * the window flashes on each press (white) and repeat (grey). Escape or
 * closing the window quits, then a summary is printed.
 *
 * Usage: sdlkeys [-o file] [-t seconds]
 *   -o  also write every key event to file: time (ms), down/repeat/up, key
 *   -t  quit by itself after this many seconds
 * Part of riscos-mesa, MIT licence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL.h"

int main(int argc, char **argv)
{
    SDL_Window *win;
    SDL_Surface *surf;
    FILE *log = NULL;
    int presses = 0, repeats = 0, releases = 0, running = 1, i;
    Uint32 flash = 0, flash_until = 0, quit_at = 0;
    char title[160], last[40] = "none";

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            log = fopen(argv[++i], "w");
            if (!log) { printf("can't write %s\n", argv[i]); return 1; }
        } else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            quit_at = (Uint32)(atof(argv[++i]) * 1000);
        }
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("sdlkeys", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 320, 160, 0);
    if (!win) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    if (quit_at) quit_at += SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        Uint32 now;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                const char *what = ev.type == SDL_KEYUP ? "up" : ev.key.repeat ? "repeat" : "down";
                snprintf(last, sizeof last, "%s", SDL_GetKeyName(ev.key.keysym.sym));
                if (ev.type == SDL_KEYUP) releases++;
                else if (ev.key.repeat) { repeats++; flash = 0x808080; flash_until = SDL_GetTicks() + 60; }
                else { presses++; flash = 0xffffff; flash_until = SDL_GetTicks() + 60; }
                if (log) fprintf(log, "%8u %-6s %s\n", (unsigned)ev.key.timestamp, what, last);
                if (ev.type == SDL_KEYDOWN && !ev.key.repeat && ev.key.keysym.sym == SDLK_ESCAPE)
                    running = 0;
            }
        }
        now = SDL_GetTicks();
        if (quit_at && SDL_TICKS_PASSED(now, quit_at)) running = 0;
        snprintf(title, sizeof title, "sdlkeys: %d down, %d repeat, %d up - last %s",
                 presses, repeats, releases, last);
        SDL_SetWindowTitle(win, title);
        surf = SDL_GetWindowSurface(win);
        if (surf) {
            Uint32 c = SDL_TICKS_PASSED(now, flash_until) ? 0x203040 : flash;
            SDL_FillRect(surf, NULL, SDL_MapRGB(surf->format, c >> 16, (c >> 8) & 0xff, c & 0xff));
            SDL_UpdateWindowSurface(win);
        }
        SDL_Delay(16);              /* about 60 polls a second, like a game */
    }
    if (log) fclose(log);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("sdlkeys: %d presses, %d repeats, %d releases\n", presses, repeats, releases);
    return 0;
}
