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
    double  pos;      /* fractional frame position in fx */
    float   gain;
};

static SDL_AudioDeviceID g_dev;
static int           g_freq;
static int           g_channels;
static Uint8        *g_wav;
static Uint32        g_len;
static Uint32        g_loop;       /* samples (interleaved) */
static Uint32        g_pos;        /* samples (interleaved) */
static Uint8        *g_fx_buf;
static const Sint16 *g_fx;
static Uint32        g_fx_frames;
static int           g_fx_ch;
static double        g_fx_step;
static float         g_gain;
static struct voice  g_voice[MAX_VOICES];

/* linear interpolation between neighbouring fx frames */
static float fx_sample(const struct voice *vc, int c)
{
    Uint32 i  = (Uint32)vc->pos;
    float  t  = (float)(vc->pos - (double)i);
    int    ch = c % g_fx_ch;
    float  a  = (float)g_fx[i * (Uint32)g_fx_ch + (Uint32)ch];
    float  b  = (float)g_fx[(i + 1) * (Uint32)g_fx_ch + (Uint32)ch];

    return a + (b - a) * t;
}

/* hum loop + flicker one-shots, master gain */
static void callback(void *ud, Uint8 *stream, int len)
{
    Sint16       *out    = (Sint16 *)stream;
    const Sint16 *hum    = (const Sint16 *)g_wav;
    int           frames = len / 2 / g_channels;
    Uint32        pos    = g_pos;
    int           f, c, v;
    float         s;

    (void)ud;

    for (f = 0; f < frames; f++) {
        for (c = 0; c < g_channels; c++) {
            s = (float)hum[pos + (Uint32)c];

            for (v = 0; v < MAX_VOICES; v++)
                if (g_voice[v].active)
                    s += fx_sample(&g_voice[v], c) * g_voice[v].gain;

            s *= g_gain;
            if (s >  32767.0f) s =  32767.0f;
            if (s < -32768.0f) s = -32768.0f;
            *out++ = (Sint16)s;
        }

        pos += (Uint32)g_channels;
        if (pos >= g_loop)
            pos = 0;

        for (v = 0; v < MAX_VOICES; v++) {
            if (!g_voice[v].active)
                continue;
            g_voice[v].pos += g_fx_step;
            if (g_voice[v].pos >= (double)(g_fx_frames - 1))
                g_voice[v].active = 0;
        }
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

static void print_peak(const char *label, const Sint16 *s, Uint32 count)
{
    Uint32 i;
    int    a, peak = 0;

    for (i = 0; i < count; i++) {
        a = s[i] < 0 ? -(int)s[i] : (int)s[i];
        if (a > peak)
            peak = a;
    }
    if (peak == 0)
        printf("%-14s %s\n", label, "silent");
    else
        printf("%-14s %.1f dBFS\n", label, 20.0 * log10((double)peak / 32768.0));
}

/* faulty.wav stays at its own rate; resampled in the callback */
static int load_fx(void)
{
    SDL_AudioSpec spec;
    SDL_RWops    *rw;
    Uint32        len;

    rw = SDL_RWFromConstMem(faulty_wav_start, (int)(faulty_wav_end - faulty_wav_start));
    if (!rw || !SDL_LoadWAV_RW(rw, 1, &spec, &g_fx_buf, &len)) {
        printf("%-14s %s\n", "FX FAIL", SDL_GetError());
        g_fx_buf = NULL;
        return 0;
    }

    if (spec.format != AUDIO_S16LSB || spec.channels < 1) {
        printf("%-14s %s\n", "FX FAIL", "wav must be 16-bit PCM");
        SDL_FreeWAV(g_fx_buf);
        g_fx_buf = NULL;
        return 0;
    }

    g_fx        = (const Sint16 *)g_fx_buf;
    g_fx_ch     = spec.channels;
    g_fx_frames = len / 2 / (Uint32)g_fx_ch;
    g_fx_step   = (double)spec.freq / (double)g_freq;

    if (g_fx_frames < 2) {
        SDL_FreeWAV(g_fx_buf);
        g_fx_buf = NULL;
        g_fx     = NULL;
        return 0;
    }

    printf("%-14s %d Hz, %d ch, %.1f s, step %.3f\n", "Flicker FX",
           spec.freq, g_fx_ch, (float)g_fx_frames / (float)spec.freq, g_fx_step);
    print_peak("FX peak", g_fx, g_fx_frames * (Uint32)g_fx_ch);
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
    print_peak("Hum peak", (const Sint16 *)g_wav, g_loop);
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

    g_dev