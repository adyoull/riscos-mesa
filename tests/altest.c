/* altest: checks OpenAL Soft from the devkit (libopenal.a), which plays
 * through SDL2's audio (on RISC OS: SDL's RISC OS driver, SharedSoundBuffer).
 *
 *   altest [-o file]
 *
 * Plays three one-second tones: 440 Hz in the middle, 660 Hz on the left,
 * 880 Hz on the right (a positioned OpenAL source each), and checks that
 * each finishes in about a second of real time, which shows that SDL's
 * audio thread is pulling OpenAL's mixed sound at the right rate. Prints
 * the OpenAL and SDL details and PASS or FAIL (exit code 0 or 1).
 *
 * The same source builds on Linux for tests/host-harness/openal, which
 * records the sound with SDL's "disk" driver and checks the tones.
 */
#include <AL/al.h>
#include <AL/alc.h>
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define RATE 22050
#define TONE_MS 1000

static FILE *out;
static int failures;

static void check(int ok, const char *what)
{
    fprintf(out, "%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        failures++;
}

/* One second of a sine tone at freq Hz, mono 16-bit, faded in and out
 * over 10 ms so it starts and stops without clicks. */
static ALuint make_tone(float freq)
{
    static short samples[RATE * TONE_MS / 1000];
    const int n = sizeof samples / sizeof samples[0], fade = RATE / 100;
    ALuint buf;
    int i;
    for (i = 0; i < n; i++) {
        float amp = 0.5f;
        if (i < fade)
            amp *= (float)i / fade;
        if (n - i < fade)
            amp *= (float)(n - i) / fade;
        samples[i] = (short)(32767 * amp * sinf(2 * (float)M_PI * freq * i / RATE));
    }
    alGenBuffers(1, &buf);
    alBufferData(buf, AL_FORMAT_MONO16, samples, sizeof samples, RATE);
    return buf;
}

/* Plays a tone from position x (-1 left, 0 middle, 1 right) and waits for
 * the source to stop. Returns how long that took, in ms. */
static Uint32 play(float freq, float x)
{
    ALuint buf = make_tone(freq), src;
    ALint state;
    Uint32 start, took;
    alGenSources(1, &src);
    alSourcei(src, AL_BUFFER, (ALint)buf);
    alSource3f(src, AL_POSITION, x, 0.0f, x == 0.0f ? -1.0f : 0.0f);
    start = SDL_GetTicks();
    alSourcePlay(src);
    do {
        SDL_Delay(10);
        alGetSourcei(src, AL_SOURCE_STATE, &state);
    } while (state == AL_PLAYING && SDL_GetTicks() - start < 5000);
    took = SDL_GetTicks() - start;
    alDeleteSources(1, &src);
    alDeleteBuffers(1, &buf);
    return took;
}

int main(int argc, char **argv)
{
    static const struct { float freq, x; const char *where; } tones[] = {
        { 440.0f, 0.0f, "middle" }, { 660.0f, -1.0f, "left" }, { 880.0f, 1.0f, "right" }
    };
    ALCdevice *dev;
    ALCcontext *ctx;
    ALCint freq = 0, refresh = 0, attrs[] = { 0 };
    char line[128];
    unsigned i;

    out = stdout;
    if (argc == 3 && strcmp(argv[1], "-o") == 0) {
        out = fopen(argv[2], "w");
        if (!out)
            return 1;
    }

    /* OpenAL initialises SDL's audio itself; SDL_Init here only so that
     * SDL_GetTicks/SDL_Delay are set up. */
    SDL_Init(SDL_INIT_TIMER);
    dev = alcOpenDevice(NULL);
    check(dev != NULL, "alcOpenDevice");
    if (!dev)
        goto done;
    fprintf(out, "     device: %s\n", alcGetString(dev, ALC_DEVICE_SPECIFIER));
    ctx = alcCreateContext(dev, attrs);
    check(ctx != NULL && alcMakeContextCurrent(ctx), "alcCreateContext + MakeContextCurrent");
    if (!ctx)
        goto close;
    alcGetIntegerv(dev, ALC_FREQUENCY, 1, &freq);
    alcGetIntegerv(dev, ALC_REFRESH, 1, &refresh);
    fprintf(out, "     OpenAL %s, %s; mixing at %d Hz, %d updates/s\n",
            alGetString(AL_VERSION), alGetString(AL_RENDERER), (int)freq, (int)refresh);
    fprintf(out, "     SDL audio driver: %s\n",
            SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "(none)");

    for (i = 0; i < sizeof tones / sizeof tones[0]; i++) {
        Uint32 took = play(tones[i].freq, tones[i].x);
        snprintf(line, sizeof line, "%.0f Hz on the %s played in %u ms (expected about %d)",
                 tones[i].freq, tones[i].where, (unsigned)took, TONE_MS);
        /* Generous limits: SDL and the sound system buffer ahead */
        check(took >= TONE_MS * 8 / 10 && took <= TONE_MS * 2, line);
    }
    check(alGetError() == AL_NO_ERROR, "no OpenAL errors");

    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
close:
    alcCloseDevice(dev);
done:
    fprintf(out, "%s\n", failures ? "FAIL" : "PASS");
    if (out != stdout)
        fclose(out);
    SDL_Quit();
    return failures ? 1 : 0;
}
