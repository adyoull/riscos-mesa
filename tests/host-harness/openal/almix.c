/*
 * almix.c - mixes a fixed scene through OpenAL Soft's loopback device
 * (ALC_SOFT_loopback) and writes the result: no audio device and no
 * timing, so two runs of the same library give the same samples, and two
 * builds (or the NEON and C mixers) can be compared sample for sample.
 *
 * The scene: three looping sources playing tones at 11025, 22050 and
 * 44100 Hz (so the mixer resamples up and down), panned left, centre and
 * right, with different gains, and the gain of one changed half way
 * (gain ramps). Output: stereo at the given rate.
 *
 *   almix int16|float32 RATE SECONDS OUTFILE
 *
 * Part of riscos-mesa's host tests (tests/host-harness/openal/neon.sh).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"

#define NSRC 3

int main(int argc, char **argv)
{
    static const ALsizei rates[NSRC] = { 11025, 22050, 44100 };
    static const float freqs[NSRC] = { 440.0f, 660.0f, 1234.5f };
    static const float pans[NSRC] = { -1.0f, 0.0f, 1.0f };
    static const float gains[NSRC] = { 0.8f, 0.5f, 0.3f };
    LPALCLOOPBACKOPENDEVICESOFT open_loopback;
    LPALCRENDERSAMPLESSOFT render;
    ALCdevice *dev;
    ALCcontext *ctx;
    ALuint buf[NSRC], src[NSRC];
    ALCint attrs[16];
    ALCenum type;
    FILE *out;
    int rate, seconds, frames, done, i, n, fsize;
    char *block;

    if (argc != 5) {
        fprintf(stderr, "usage: almix int16|float32 RATE SECONDS OUTFILE\n");
        return 2;
    }
    type = strcmp(argv[1], "float32") == 0 ? ALC_FLOAT_SOFT : ALC_SHORT_SOFT;
    fsize = type == ALC_FLOAT_SOFT ? 8 : 4;
    rate = atoi(argv[2]);
    seconds = atoi(argv[3]);

    open_loopback = (LPALCLOOPBACKOPENDEVICESOFT) alcGetProcAddress(NULL, "alcLoopbackOpenDeviceSOFT");
    render = (LPALCRENDERSAMPLESSOFT) alcGetProcAddress(NULL, "alcRenderSamplesSOFT");
    if (!open_loopback || !render || !(dev = open_loopback(NULL))) {
        fprintf(stderr, "no loopback device\n");
        return 1;
    }
    i = 0;
    attrs[i++] = ALC_FORMAT_CHANNELS_SOFT; attrs[i++] = ALC_STEREO_SOFT;
    attrs[i++] = ALC_FORMAT_TYPE_SOFT;     attrs[i++] = type;
    attrs[i++] = ALC_FREQUENCY;            attrs[i++] = rate;
    attrs[i++] = 0;
    if (!(ctx = alcCreateContext(dev, attrs)) || !alcMakeContextCurrent(ctx)) {
        fprintf(stderr, "no context\n");
        return 1;
    }

    alGenBuffers(NSRC, buf);
    alGenSources(NSRC, src);
    for (n = 0; n < NSRC; n++) {
        /* one second of the tone, a whole number of cycles near enough */
        short *pcm = malloc(sizeof(short) * rates[n]);
        for (i = 0; i < rates[n]; i++)
            pcm[i] = (short) lrintf(20000.0f * sinf(2.0f * 3.14159265f * freqs[n] * i / rates[n]));
        alBufferData(buf[n], AL_FORMAT_MONO16, pcm, sizeof(short) * rates[n], rates[n]);
        free(pcm);
        alSourcei(src[n], AL_BUFFER, buf[n]);
        alSourcei(src[n], AL_LOOPING, AL_TRUE);
        alSource3f(src[n], AL_POSITION, pans[n], 0.0f, -1.0f);
        alSourcef(src[n], AL_GAIN, gains[n]);
    }
    alSourcePlayv(NSRC, src);

    if (!(out = fopen(argv[4], "wb")))
        return 1;
    frames = rate * seconds;
    block = malloc((size_t) fsize * 1024);
    for (done = 0; done < frames; done += n) {
        n = frames - done < 1024 ? frames - done : 1024;
        if (done >= frames / 2 && done - 1024 < frames / 2)
            alSourcef(src[1], AL_GAIN, 0.9f);       /* a gain ramp half way */
        render(dev, block, n);
        fwrite(block, (size_t) fsize, (size_t) n, out);
    }
    fclose(out);
    free(block);

    alDeleteSources(NSRC, src);
    alDeleteBuffers(NSRC, buf);
    i = alGetError();
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    alcCloseDevice(dev);
    if (i != AL_NO_ERROR)
        fprintf(stderr, "AL error 0x%x\n", i);
    return i == AL_NO_ERROR ? 0 : 1;
}
