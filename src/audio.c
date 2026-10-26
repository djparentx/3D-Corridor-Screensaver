#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "audio.h"

extern const uint8_t hum_wav_start[];
extern const uint8_t hum_wav_end[];

static SDL_AudioDeviceID g_dev;
static Uint8  *g_wav;
static Uint32  g_len;
static Uint32  g_pos;
static float   g_gain;
static Uint32  g_loop;

#define XFADE_MS 200
/* loop the 16-bit PCM buffer forever with gain */
static void callback(void *ud, Uint8 *stream, int len)
{
    Sint16       *out   = (Sint16 *)stream;
    const Sint16 *src   = (const Sint16 *)g_wav;
    Uint32        total = g_loop;
    Uint32        pos   = g_pos;
    int           n     = len / 2, i;
    float         s;

    (void)ud;

    for (i = 0; i < n; i++) {
        s = (float)src[pos] * g_gain;
        if (s >  32767.0f) s =  32767.0f;
        if (s < -32768.0f) s = -32768.0f;
        out[i] = (Sint16)s;
        if (++pos >= total)
            pos = 0;
    }
    g_pos = pos;
}

/* blend the last XFADE_MS into the first XFADE_MS (equal power),
   then loop only the first (frames - fade) frames */
static void crossfade_loop(int channels, int freq)
{
    Sint16 *s      = (Sint16 *)g_wav;
    Uint32  frames = g_len / 2 / (Uint32)channels;
    Uint32  fade   = (Uint32)freq * XFADE_MS / 1000;
    Uint32  keep, i;
    float   t, gin, gout, v;
    int     c;

    if (fade * 2 > frames)
        fade = frames / 2;
    keep = frames - fade;

    for (i = 0; i < fade; i++) {
        t    = (float)i / (float)fade;
        gin  = sinf(t * (float)M_PI * 0.5f);
        gout = cosf(t * (float)M_PI * 0.5f);
        for (c = 0; c < channels; c++) {
            v = (float)s[i * channels + c] * gin +
                (float)s[(keep + i) * channels + c] * gout;
            if (v >  32767.0f) v =  32767.0f;
            if (v < -32768.0f) v = -32768.0f;
            s[i * channels + c] = (Sint16)v;
        }
    }

    g_loop = keep * (Uint32)channels;
}

int audio_start(int volume)
{
    SDL_AudioSpec wav, want, have;
    SDL_RWops *rw;

    if (volume <= 0)
        return 0;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("%-14s %s\n", "AUDIO FAIL", SDL_GetError());
        return 0;
    }

    rw = SDL_RWFromConstMem(hum_wav_start, (int)(hum_wav_end - hum_wav_start));
    if (!rw || !SDL_LoadWAV_RW(rw, 1, &wav, &g_wav, &g_len)) {
        printf("%-14s %s\n", "AUDIO FAIL", SDL_GetError());
        return 0;
    }

    if (wav.format != AUDIO_S16LSB || g_len < 2) {
        printf("%-14s %s\n", "AUDIO FAIL", "wav must be 16-bit PCM");
        SDL_FreeWAV(g_wav);
        g_wav = NULL;
        return 0;
    }

    crossfade_loop(wav.channels, wav.freq);

    g_gain = powf(10.0f, (float)((volume - 3) * 3) / 20.0f);
    g_pos  = 0;

    SDL_zero(want);
    want.freq     = wav.freq;
    want.format   = AUDIO_S16LSB;
    want.channels = wav.channels;
    want.samples  = 2048;
    want.callback = callback;

    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_dev) {
        printf("%-14s %s\n", "AUDIO FAIL", SDL_GetError());
        SDL_FreeWAV(g_wav);
        g_wav = NULL;
        return 0;
    }

    SDL_PauseAudioDevice(g_dev, 0);
    printf("%-14s %d Hz, %d ch, %+d dB\n", "Audio",
           want.freq, want.channels, (volume - 3) * 3);
    return 1;
}

void audio_stop(void)
{
    if (g_dev) {
        SDL_CloseAudioDevice(g_dev);
        g_dev = 0;
    }
    if (g_wav) {
        SDL_FreeWAV(g_wav);
        g_wav = NULL;
    }
}
