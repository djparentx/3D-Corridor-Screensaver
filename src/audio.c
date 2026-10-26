#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "audio.h"

extern const uint8_t hum_wav_start[];
extern const uint8_t hum_wav_end[];
extern const uint8_t faulty_wav_start[];
extern const uint8_t faulty_wav_end[];

#define XFADE_MS    200
#define MAX_VOICES  4

struct voice {
    int     active;
    int32_t pi, pj;
    double  x, z;
    Uint32  pos;
    float   gain;
};

static SDL_AudioDeviceID g_dev;
static int           g_freq;
static int           g_channels;
static Uint8        *g_wav;
static Uint32        g_len;
static Uint32        g_loop;
static Uint32        g_pos;
static Sint16       *g_fx;
static Uint32        g_fx_len;
static float         g_gain;
static struct voice  g_voice[MAX_VOICES];

/* hum loop + flicker one-shots, master gain */
static void callback(void *ud, Uint8 *stream, int len)
{
    Sint16       *out = (Sint16 *)stream;
    const Sint16 *hum = (const Sint16 *)g_wav;
    Uint32        pos = g_pos;
    int           n   = len / 2, i, v;
    float         s;

    (void)ud;

    for (i = 0; i < n; i++) {
        s = (float)hum[pos];
        if (++pos >= g_loop)
            pos = 0;

        for (v = 0; v < MAX_VOICES; v++) {
            struct voice *vc = &g_voice[v];

            if (!vc->active)
                continue;
            s += (float)g_fx[vc->pos] * vc->gain;
            if (++vc->pos >= g_fx_len)
                vc->active = 0;
        }

        s *= g_gain;
        if (s >  32767.0f) s =  32767.0f;
        if (s < -32768.0f) s = -32768.0f;
        out[i] = (Sint16)s;
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

/* load faulty.wav and convert it to the hum's rate/channels, 16-bit */
static int load_fx(void)
{
    SDL_AudioSpec spec;
    SDL_AudioCVT  cvt;
    SDL_RWops    *rw;
    Uint8        *buf;
    Uint32        len;

    rw = SDL_RWFromConstMem(faulty_wav_start, (int)(faulty_wav_end - faulty_wav_start));
    if (!rw || !SDL_LoadWAV_RW(rw, 1, &spec, &buf, &len)) {
        printf("%-14s %s\n", "FX FAIL", SDL_GetError());
        return 0;
    }

    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq,
                          AUDIO_S16LSB, (Uint8)g_channels, g_freq) < 0) {
        printf("%-14s %s\n", "FX FAIL", SDL_GetError());
        SDL_FreeWAV(buf);
        return 0;
    }

    cvt.len = (int)len;
    cvt.buf = SDL_malloc((size_t)len * (size_t)cvt.len_mult);
    if (!cvt.buf) {
        printf("%-14s %s\n", "FX FAIL", "out of memory");
        SDL_FreeWAV(buf);
        return 0;
    }
    SDL_memcpy(cvt.buf, buf, len);
    SDL_FreeWAV(buf);

    if (cvt.needed) {
        if (SDL_ConvertAudio(&cvt) != 0) {
            printf("%-14s %s\n", "FX FAIL", SDL_GetError());
            SDL_free(cvt.buf);
            return 0;
        }
    } else {
        cvt.len_cvt = cvt.len;
    }

    g_fx     = (Sint16 *)cvt.buf;
    g_fx_len = (Uint32)cvt.len_cvt / 2;
    if (g_fx_len == 0) {
        SDL_free(g_fx);
        g_fx = NULL;
        return 0;
    }

    printf("%-14s %d Hz -> %d Hz, %.1f s\n", "Flicker FX",
           spec.freq, g_freq, (float)g_fx_len / (float)(g_freq * g_channels));
    return 1;
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

    g_freq     = wav.freq;
    g_channels = wav.channels;
    crossfade_loop(wav.channels, wav.freq);
    load_fx();

    g_gain = powf(10.0f, (float)((volume - 3) * 3) / 20.0f);
    g_pos  = 0;
    SDL_memset(g_voice, 0, sizeof(g_voice));

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
        if (g_fx) {
            SDL_free(g_fx);
            g_fx = NULL;
        }
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
    if (g_fx) {
        SDL_free(g_fx);
        g_fx = NULL;
    }
}

void audio_flicker(int32_t pi, int32_t pj, double x, double z)
{
    int v, slot = -1;

    if (!g_dev || !g_fx)
        return;

    SDL_LockAudioDevice(g_dev);
    for (v = 0; v < MAX_VOICES; v++) {
        if (g_voice[v].active && g_voice[v].pi == pi && g_voice[v].pj == pj) {
            SDL_UnlockAudioDevice(g_dev);
            return;
        }
        if (!g_voice[v].active && slot < 0)
            slot = v;
    }
    if (slot >= 0) {
        g_voice[slot].pi     = pi;
        g_voice[slot].pj     = pj;
        g_voice[slot].x      = x;
        g_voice[slot].z      = z;
        g_voice[slot].pos    = 0;
        g_voice[slot].gain   = 0.0f;
        g_voice[slot].active = 1;
    }
    SDL_UnlockAudioDevice(g_dev);
}

void audio_listener(double x, double z)
{
    double d, t;
    int v;

    if (!g_dev || !g_fx)
        return;

    SDL_LockAudioDevice(g_dev);
    for (v = 0; v < MAX_VOICES; v++) {
        if (!g_voice[v].active)
            continue;
        d = hypot(g_voice[v].x - x, g_voice[v].z - z);
        t = 1.0 - d / FLICKER_RANGE;
        if (t < 0.0)
            t = 0.0;
        g_voice[v].gain = (float)(t * t);
    }
    SDL_UnlockAudioDevice(g_dev);
}