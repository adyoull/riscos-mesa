/*
 * Example 6: sound with OpenAL - a three-note tune that moves from left
 * to right.
 *
 * OpenAL is to sound what OpenGL is to pictures: you describe sounds
 * ("buffers") and where they are ("sources"), and it mixes them, pans them
 * between the speakers and plays them. Games use it for positioned sound
 * effects. Here it plays through SDL's RISC OS sound driver, which shares
 * the sound system politely with other programs.
 *
 * What you learn here: making a sound in memory, playing it from a
 * position, and waiting for it to finish.
 *
 * Why a separate thread matters: OpenAL mixes in the background, in a
 * second thread that SDL starts. Programs with more than one thread
 * should be linked with a UnixLib that has the pthread ticker fix
 * (riscos-unixlib 0.1.1 or later); older ones can crash other programs
 * while this one runs in the desktop. See ../README.md.
 *
 * There's no window: it plays for about two seconds and exits. It isn't a
 * desktop task either (it never calls Wimp_Initialise), so the desktop
 * pauses while it plays. A real program would play sound from its poll
 * loop, the way example 2 draws.
 *
 * Build: make 6-sound   (see ../README.md)
 *
 * Part of the riscos-mesa devkit. MIT licence: copy it, change it, use it
 * as the start of your own program.
 */
#include <math.h>
#include <AL/al.h>
#include <AL/alc.h>
#include <stdio.h>
#include <SDL.h>                 /* only for SDL_Delay: waiting without
                                    keeping the processor busy */

#define RATE 22050               /* samples per second */

/* Makes a buffer holding a note: "ms" milliseconds of a sine wave at
 * "freq" Hz, faded in and out so it doesn't click. Mono (one channel):
 * OpenAL can only place one-channel sounds in space; stereo sounds play
 * as they are. */
static ALuint make_note(float freq, int ms)
{
    static short samples[RATE];  /* room for up to one second */
    int n = RATE * ms / 1000, fade = RATE / 100, i;
    if (n > RATE)
        n = RATE;                /* the buffer holds one second at most */
    ALuint buffer;
    for (i = 0; i < n; i++) {
        float volume = 0.4f;
        if (i < fade) volume *= (float) i / fade;
        if (n - i < fade) volume *= (float) (n - i) / fade;
        samples[i] = (short) (32767 * volume * sinf(6.2831853f * freq * i / RATE));
    }
    alGenBuffers(1, &buffer);
    alBufferData(buffer, AL_FORMAT_MONO16, samples, n * sizeof(short), RATE);
    return buffer;
}

int main(int argc, char **argv)
{
    /* Doh, me, soh: left, middle, right. */
    static const float notes[] = { 523.25f, 659.25f, 783.99f };
    static const float places[] = { -1.0f, 0.0f, 1.0f };
    ALCdevice *device;
    ALCcontext *context;
    ALuint source;
    int i;

    (void) argc; (void) argv;
    SDL_Init(SDL_INIT_TIMER);             /* for SDL_Delay */

    /* Open the sound output and make a "context" (OpenAL's world, like
     * GL's context). NULL = the default device. */
    device = alcOpenDevice(NULL);
    if (!device) {
        printf("No sound output: is SharedSoundBuffer or DigitalRenderer loaded?\n");
        return 1;
    }
    context = alcCreateContext(device, NULL);
    alcMakeContextCurrent(context);

    alGenSources(1, &source);             /* one thing that makes sound */
    for (i = 0; i < 3; i++) {
        ALuint note = make_note(notes[i], 500);
        ALint state;

        alSourcei(source, AL_BUFFER, (ALint) note);
        /* Where the sound is: x from -1 (left) to 1 (right), a little in
         * front of the listener (z = -1) so the middle note is centred. */
        alSource3f(source, AL_POSITION, places[i], 0.0f, -1.0f);
        alSourcePlay(source);
        do {                              /* wait for the note to end */
            SDL_Delay(10);
            alGetSourcei(source, AL_SOURCE_STATE, &state);
        } while (state == AL_PLAYING);
        alSourcei(source, AL_BUFFER, 0);  /* let go of the buffer... */
        alDeleteBuffers(1, &note);        /* ...before throwing it away */
    }

    alDeleteSources(1, &source);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(context);
    alcCloseDevice(device);
    SDL_Quit();
    return 0;
}
