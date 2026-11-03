/* SDL_SemWaitTimeout with and without a working sem_timedwait (run.sh).
 *
 * UnixLib before 5.0.3.6 has sem_timedwait only as a stub that always
 * fails with ENOSYS. An SDL built with a newer UnixLib calls it, and may
 * be linked into a program with an older one, so the RISC OS overlay's
 * SDL_syssem.c falls back to SDL's polling wait when it sees ENOSYS.
 * This program is linked with -Wl,--wrap=sem_timedwait, so every call SDL
 * makes comes here: SEMTEST_STUB=1 answers as the old stub does, otherwise
 * the C library's real sem_timedwait is used. In both cases a wait must
 * time out after about its timeout (not at once), and a post from another
 * thread must end it early. */
#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "SDL.h"

static int stub;       /* answer as UnixLib's old stub */
static int calls;      /* calls SDL made to sem_timedwait */
static int failures;

int __real_sem_timedwait(sem_t *sem, const struct timespec *abstime);
int __wrap_sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
    calls++;
    if (stub) {
        errno = ENOSYS;
        return -1;
    }
    return __real_sem_timedwait(sem, abstime);
}

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

static SDL_sem *sem;

static int post_after_100ms(void *unused)
{
    (void)unused;
    SDL_Delay(100);
    SDL_SemPost(sem);
    return 0;
}

/* One wait of 300 ms on an empty semaphore, with or without a post at
   100 ms from another thread */
static void wait_case(const char *mode, int post)
{
    char what[160];
    SDL_Thread *t = NULL;
    Uint64 t0, ms;
    int r;

    calls = 0;
    if (post)
        t = SDL_CreateThread(post_after_100ms, "post", NULL);
    t0 = SDL_GetTicks64();
    r = SDL_SemWaitTimeout(sem, 300);
    ms = SDL_GetTicks64() - t0;
    if (t)
        SDL_WaitThread(t, NULL);

    if (post) {
        snprintf(what, sizeof what, "%s: a post at 100 ms ends a 300 ms wait (returned %d after %d ms)",
                 mode, r, (int)ms);
        check(r == 0 && ms >= 90 && ms < 250, what);
    } else {
        snprintf(what, sizeof what, "%s: an empty semaphore times out after 300 ms (returned %d after %d ms)",
                 mode, r, (int)ms);
        check(r == SDL_MUTEX_TIMEDOUT && ms >= 295 && ms < 450, what);
    }
    snprintf(what, sizeof what, "%s: SDL called sem_timedwait (%d calls)", mode, calls);
    check(calls >= 1, what);
}

int main(void)
{
    const char *mode;

    stub = getenv("SEMTEST_STUB") != NULL;
    mode = stub ? "ENOSYS stub" : "real sem_timedwait";
    if (SDL_Init(SDL_INIT_TIMER) < 0) {
        printf("FAIL SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    sem = SDL_CreateSemaphore(0);
    wait_case(mode, 0);
    wait_case(mode, 1);
    SDL_DestroySemaphore(sem);
    SDL_Quit();
    printf("%s: %d failures\n", mode, failures);
    return failures != 0;
}
