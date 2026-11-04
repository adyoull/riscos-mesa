/*
 * audio-queue.c - host test of the SDL RISC OS audio driver
 * (SDL_riscosaudio.c): how much sound it keeps queued in StreamManager.
 *
 * The queue is set with SDL_RISCOS_AUDIO_QUEUE_MS (a hint, or a system
 * variable of that name), default 150 ms, never less than three of SDL's
 * buffers. The driver file is compiled in here (#include) and run against
 * a fake SharedSoundBuffer / StreamManager that plays the queued sound in
 * real time on a fake clock: SDL_Delay is the only thing that moves time
 * on, as on RISC OS the queue drains while the audio thread sleeps.
 * See run.sh. Part of riscos-mesa, MIT licence.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL_riscosaudio.c"

static int checks, failures;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- fake clock and sound modules ---- */

static double now_ms;              /* the fake clock */
static Uint32 freq = 44100;        /* the rate the stream plays at */
static double added, played;       /* bytes given to StreamManager / played */
static int paused = 1, ran_dry;    /* ran_dry: played past the end while running */
static Uint32 sm_buffer;           /* StreamManager_SetBuffer's size */

/* Play what's queued for the time that has passed */
static void play_until(double ms)
{
    if (!paused) {
        double want = played + (ms - now_ms) * freq * 4 / 1000.0;
        if (want > added) { if (added > 0) ran_dry++; want = added; }
        played = want;
    }
    now_ms = ms;
}

#define XOS_SWINumberFromString (0x20000 | OS_SWINumberFromString)
_kernel_oserror *_kernel_swi(int no, _kernel_swi_regs *in, _kernel_swi_regs *out)
{
    if (out != in) *out = *in;
    switch (no) {
    case OS_SWINumberFromString:
    case XOS_SWINumberFromString:
        break;                                       /* both modules "loaded" */
    case XSharedSoundBuffer_OpenStream:        out->r[0] = 1; break;
    case XSharedSoundBuffer_ReturnStreamHandle: out->r[0] = 2; break;
    case XSharedSoundBuffer_Pause:             paused = in->r[1] == 0; break;
    case XStreamManager_SetBuffer:             sm_buffer = (Uint32)in->r[1]; break;
    case XStreamManager_AddBlock:              added += in->r[2]; break;
    case XStreamManager_BufferStats:
        out->r[0] = (int)added; out->r[1] = (int)played; break;
    default:
        break;
    }
    return NULL;
}

/* ---- the SDL functions the driver calls ---- */

static const char *queue_hint;     /* SDL_RISCOS_AUDIO_QUEUE_MS, or NULL */
const char *SDL_GetHint(const char *name)
{
    return strcmp(name, SDL_HINT_RISCOS_AUDIO_QUEUE_MS) == 0 ? queue_hint : NULL;
}
void SDL_Delay(Uint32 ms) { play_until(now_ms + ms); }
void *SDL_calloc(size_t n, size_t s) { return calloc(n, s); }
void *SDL_malloc(size_t s) { return malloc(s); }
void SDL_free(void *p) { free(p); }
void *SDL_memset(void *d, int c, size_t n) { return memset(d, c, n); }
long SDL_strtol(const char *s, char **e, int b) { return strtol(s, e, b); }
int SDL_SetError(const char *fmt, ...) { (void)fmt; return -1; }
int SDL_Error(SDL_errorcode c) { (void)c; return -1; }
void SDL_CalculateAudioSpec(SDL_AudioSpec *spec)
{
    spec->size = (Uint32)spec->samples * spec->channels * 2;   /* 16-bit */
}
char *program_invocation_short_name = "audio-queue";

/* ---- tests ---- */

static SDL_AudioDevice dev;

/* Open the device with SDL buffers of `samples`, with the hint set to
   `hint` (NULL: not set). Returns the queue target in milliseconds. */
static double open_with(Uint16 samples, const char *hint)
{
    memset(&dev, 0, sizeof dev);
    dev.spec.freq = (int)freq;
    dev.spec.samples = samples;
    dev.spec.channels = 2;
    queue_hint = hint;
    now_ms = added = played = 0;
    paused = 1;
    ran_dry = 0;
    if (RISCOS_OpenDevice(&dev, NULL) != 0)
        return -1;
    return dev.hidden->target * 1000.0 / (freq * 4);
}

/* Run the audio thread's loop (WaitDevice, mix, PlayDevice) for `ms` of
   fake time; a mix itself takes no time. Returns the least and most
   queued (in ms) after the first second. */
static void run_for(double ms, double *least, double *most)
{
    double end = now_ms + ms, q;
    *least = 1e9; *most = 0;
    while (now_ms < end) {
        RISCOS_WaitDevice(&dev);
        RISCOS_PlayDevice(&dev);
        if (now_ms > 1000) {
            q = (added - played) * 1000.0 / (freq * 4);
            if (q < *least) *least = q;
            if (q > *most) *most = q;
        }
        if (!dev.hidden->started) play_until(now_ms + 0.01);
    }
}

static void close_dev(void) { RISCOS_CloseDevice(&dev); }

int main(void)
{
    double t, least, most;
    double buf1024 = 1024 * 1000.0 / 44100, buf4096 = 4096 * 1000.0 / 44100;

    /* The target */
    t = open_with(1024, NULL);
    CHECK(t > 149.9 && t < 150.1, "default: 150 ms queued with 1024-sample buffers (got %.1f)", t);
    close_dev();
    t = open_with(1024, "280");
    CHECK(t > 279.9 && t < 280.1, "SDL_RISCOS_AUDIO_QUEUE_MS=280 (got %.1f)", t);
    close_dev();
    t = open_with(1024, "5");
    CHECK(t > 3 * buf1024 - 0.1 && t < 3 * buf1024 + 0.1,
          "5 ms is raised to 20 ms, and then to three buffers (got %.1f)", t);
    close_dev();
    t = open_with(256, "5");
    CHECK(t > 19.9 && t < 20.1, "5 ms with small buffers: the 20 ms minimum (got %.1f)", t);
    close_dev();
    t = open_with(1024, "99999");
    CHECK(t > 1999.9 && t < 2000.1, "very large: 2000 ms at most (got %.1f)", t);
    close_dev();
    t = open_with(1024, "fast");
    CHECK(t > 149.9 && t < 150.1, "not a number: the default (got %.1f)", t);
    close_dev();
    t = open_with(4096, NULL);
    CHECK(t > 3 * buf4096 - 0.1 && t < 3 * buf4096 + 0.1,
          "4096-sample buffers (OpenTTD's): three buffers, about 279 ms, as before (got %.1f)", t);
    CHECK(sm_buffer >= dev.hidden->target * 4, "StreamManager's buffer has room for the queue");
    close_dev();

    /* In use: the queue stays at the target, playing never runs dry */
    open_with(1024, NULL);
    run_for(5000, &least, &most);
    CHECK(ran_dry == 0, "default: never ran dry in 5 s (%d times)", ran_dry);
    CHECK(least >= 150 - buf1024 - 1 && most <= 150 + buf1024 + 1,
          "default: queued %.0f-%.0f ms, about 150", least, most);
    close_dev();
    open_with(1024, "300");
    run_for(5000, &least, &most);
    CHECK(ran_dry == 0, "300 ms: never ran dry (%d times)", ran_dry);
    CHECK(least >= 300 - buf1024 - 1 && most <= 300 + buf1024 + 1,
          "300 ms: queued %.0f-%.0f ms", least, most);
    close_dev();

    /* A stall: the audio thread doesn't run for a while. With 150 ms
       queued a 120 ms stall is ridden out; a 250 ms one isn't. */
    open_with(1024, NULL);
    run_for(2000, &least, &most);
    play_until(now_ms + 120);
    CHECK(ran_dry == 0, "150 ms queued rides out a 120 ms stall");
    run_for(1000, &least, &most);
    play_until(now_ms + 250);
    CHECK(ran_dry > 0, "but not a 250 ms one");
    close_dev();
    open_with(1024, "300");
    run_for(2000, &least, &most);
    play_until(now_ms + 250);
    CHECK(ran_dry == 0, "with 300 ms queued, a 250 ms stall is ridden out");
    close_dev();

    printf("%d checks, %d failures: %s\n", checks, failures, failures ? "FAILED" : "ALL PASS");
    return failures != 0;
}
