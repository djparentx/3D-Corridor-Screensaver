#include <SDL2/SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include "audio.h"

extern const uint8_t hum_wav_start[];
extern const uint8_t hum_wav_end[];
extern const uint8_t faulty_wav_start[];
extern const uint8_t faulty_wav_end[];
extern const uint8_t phone_wav_start[], phone_wav_end[];
extern const uint8_t knock_wav_start[], knock_wav_end[];

#define XFADE_MS          200
#define MAX_VOICES        4
#define LIMIT_DB          -3.0f    /* mix ceiling, leaves room for device resampling */
#define LIMIT_RELEASE_MS  100.0f
#define HUM_ENABLED       1        /* test: 0 = hum muted, 1 = normal */

struct voice {
    int     active;
    int32_t pi, pj;
    double  x, z;
    double  pos;      /* fractional frame position in fx */
    float   gain;     /* distance gain x fx scale */
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
static float         g_fx_peak;
static float         g_fx_scale = 1.0f;
static Uint8        *g_phone_buf;
static const Sint16 *g_phone;
static Uint32        g_phone_frames;
static int           g_phone_ch;
static double        g_phone_step;
static float         g_phone_scale = 1.0f;
static float         g_gain;
static float         g_lim      = 1.0f;
static float         g_lim_thr;
static float         g_lim_rel;
static struct voice  g_voice[MAX_VOICES];
#define PHONE_INTERVAL_MS  60000
#define PHONE_MIN_RINGS    4
#define PHONE_MAX_RINGS    12
#define PHONE_RANGE        30.0

static double g_phone_x, g_phone_z;
static double g_phone_pos;
static float  g_phone_gain;
static int    g_phone_active;
static int    g_phone_rings_left;
static Uint32 g_phone_next_check;
static Uint32 g_phone_rng;
static Uint8 *g_knock_buf;
static const Sint16 *g_knock;
static Uint32 g_knock_frames;
static int g_knock_ch;
static double g_knock_step;
static float g_knock_scale = 1.0f;
static double g_knock_pos;
static int g_knock_active;
static Uint32 g_knock_next_check;

static Uint32 phone_random(void)
{
    g_phone_rng ^= g_phone_rng << 13;
    g_phone_rng ^= g_phone_rng >> 17;
    g_phone_rng ^= g_phone_rng << 5;
    return g_phone_rng;
}

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

static float phone_sample(double pos, int c)
{
    Uint32 i = (Uint32)pos;
    float t = (float)(pos - (double)i);
    int ch = c % g_phone_ch;
    float a = (float)g_phone[i * (Uint32)g_phone_ch + (Uint32)ch];
    float b = (float)g_phone[(i + 1) * (Uint32)g_phone_ch + (Uint32)ch];

    return a + (b - a) * t;
}

/* hum loop + flicker one-shots, master gain, peak limiter */
static void callback(void *ud, Uint8 *stream, int len)
{
    Sint16       *out    = (Sint16 *)stream;
    const Sint16 *hum    = (const Sint16 *)g_wav;
    int           frames = len / 2 / g_channels;
    Uint32        pos    = g_pos;
    int           f, c, v;
    float         s, a;

    (void)ud;

    for (f = 0; f < frames; f++) {
        for (c = 0; c < g_channels; c++) {
            s = HUM_ENABLED ? (float)hum[pos + (Uint32)c] : 0.0f;
            if (g_phone_active && g_phone)
                s += phone_sample(g_phone_pos, c) * g_phone_gain;
            if (g_knock_active && g_knock) {
                Uint32 frame = (Uint32)g_knock_pos;
                Uint32 next = frame + 1;
                float frac = (float)(g_knock_pos - (double)frame);
                int ch0 = c % g_knock_ch;

                if (next >= g_knock_frames)
                    next = g_knock_frames - 1;

                s += ((float)g_knock[frame * g_knock_ch + ch0] *
                      (1.0f - frac) +
                      (float)g_knock[next * g_knock_ch + ch0] * frac)
                     * g_knock_scale;
            }

            for (v = 0; v < MAX_VOICES; v++)
                if (g_voice[v].active)
                    s += fx_sample(&g_voice[v], c) * g_voice[v].gain;

            s *= g_gain;

            /* instant attack, smooth release */
            a = fabsf(s);
            if (a * g_lim > g_lim_thr)
                g_lim = g_lim_thr / a;
            else
                g_lim += (1.0f - g_lim) * g_lim_rel;
            s *= g_lim;

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

        if (g_phone_active) {
            g_phone_pos += g_phone_step;
            if (g_phone_pos >= (double)(g_phone_frames - 1)) {
                if (g_phone_rings_left > 1) {
                    g_phone_rings_left--;
                    g_phone_pos = 0.0;
                } else {
                    g_phone_active = 0;
                    g_phone_rings_left = 0;
                }
            }
        }

        if (g_knock_active) {
            g_knock_pos += g_knock_step;
            if (g_knock_pos >= (double)(g_knock_frames - 1))
                g_knock_active = 0;
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

/* prints and returns peak as 0..1 of full scale */
static float print_peak(const char *label, const Sint16 *s, Uint32 count)
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
    return (float)peak / 32768.0f;
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
    g_fx_peak = print_peak("FX peak", g_fx, g_fx_frames * (Uint32)g_fx_ch);
    return 1;
}

static int load_phone(void)
{
    SDL_AudioSpec spec;
    SDL_RWops    *rw;
    Uint32        len;

    rw = SDL_RWFromConstMem(phone_wav_start,
                            (int)(phone_wav_end - phone_wav_start));
    if (!rw || !SDL_LoadWAV_RW(rw, 1, &spec, &g_phone_buf, &len)) {
        printf("%-14s %s\n", "PHONE FAIL", SDL_GetError());
        g_phone_buf = NULL;
        return 0;
    }

    if (spec.format != AUDIO_S16LSB || spec.channels < 1) {
        printf("%-14s %s\n", "PHONE FAIL", "wav must be 16-bit PCM");
        SDL_FreeWAV(g_phone_buf);
        g_phone_buf = NULL;
        return 0;
    }

    g_phone        = (const Sint16 *)g_phone_buf;
    g_phone_ch     = spec.channels;
    g_phone_frames = len / 2 / (Uint32)g_phone_ch;
    g_phone_step   = (double)spec.freq / (double)g_freq;

    if (g_phone_frames < 2) {
        SDL_FreeWAV(g_phone_buf);
        g_phone_buf = NULL;
        g_phone     = NULL;
        return 0;
    }
}

static int load_knock(void)
{
    SDL_AudioSpec spec;
    SDL_RWops *rw;
    Uint32 len;

    rw = SDL_RWFromConstMem(knock_wav_start,
                            (int)(knock_wav_end - knock_wav_start));
    if (!rw || !SDL_LoadWAV_RW(rw, 1, &spec, &g_knock_buf, &len)) {
        printf("%-14s %s\n", "KNOCK FAIL", SDL_GetError());
        g_knock_buf = NULL;
        return 0;
    }

    if (spec.format != AUDIO_S16LSB || spec.channels < 1) {
        printf("%-14s %s\n", "KNOCK FAIL", "wav must be 16-bit PCM");
        SDL_FreeWAV(g_knock_buf);
        g_knock_buf = NULL;
        return 0;
    }

    g_knock = (const Sint16 *)g_knock_buf;
    g_knock_ch = spec.channels;
    g_knock_frames = len / 2 / (Uint32)g_knock_ch;
    g_knock_step = (double)spec.freq / (double)g_freq;

    if (g_knock_frames < 2) {
        SDL_FreeWAV(g_knock_buf);
        g_knock_buf = NULL;
        g_knock = NULL;
        return 0;
    }

    printf("%-14s %d Hz, %d ch, %.1f s, step %.3f\n", "Knock FX",
           spec.freq, g_knock_ch,
           (float)g_knock_frames / (float)spec.freq, g_knock_step);

    g_knock_scale = 0.5f / print_peak("Knock peak", g_knock,
                                     g_knock_frames * (Uint32)g_knock_ch);
    return 1;
}

    printf("%-14s %d Hz, %d ch, %.1f s, step %.3f\n", "Phone FX",
           spec.freq, g_phone_ch,
           (float)g_phone_frames / (float)spec.freq, g_phone_step);

    g_phone_scale = 0.5f / print_peak("Phone peak", g_phone,
                                     g_phone_frames * (Uint32)g_phone_ch);
    return 1;
}

int audio_start(int volume)
{
    SDL_AudioSpec wav, want, have;
    SDL_RWops *rw;
    float hum_peak, target;

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
    hum_peak = print_peak("Hum peak", (const Sint16 *)g_wav, g_loop);

    target    = powf(10.0f, LIMIT_DB / 20.0f);
    g_lim_thr = target * 32768.0f;
    g_lim_rel = 1.0f - expf(-1.0f / (LIMIT_RELEASE_MS / 1000.0f *
                                     (float)g_freq * (float)g_channels));
    g_lim     = 1.0f;

    if (load_fx() && g_fx_peak > 0.0f) {
        g_fx_scale = (target - hum_peak) / g_fx_peak;
        if (g_fx_scale > 1.0f)  g_fx_scale = 1.0f;
        if (g_fx_scale < 0.05f) g_fx_scale = 0.05f;
        printf("%-14s %.1f dB\n", "FX scale", 20.0f * log10f(g_fx_scale));
    }

    if (!load_phone())
        g_phone = NULL;
    g_phone_active = 0;
    g_phone_pos = 0.0;
    g_phone_rings_left = 0;

    if (!load_knock())
        g_knock = NULL;
    g_knock_active = 0;
    g_knock_pos = 0.0;

    g_gain = powf(10.0f, (float)((volume - 3) * 3) / 20.0f);
    g_pos  = 0;
    SDL_memset(g_voice, 0, sizeof(g_voice));
    g_phone_gain = 0.0f;
    g_phone_x = 0.0;
    g_phone_z = 0.0;
    g_phone_rng = SDL_GetTicks() ^ (Uint32)SDL_GetPerformanceCounter();
    if (!g_phone_rng)
        g_phone_rng = 1;
    g_phone_next_check = SDL_GetTicks() + PHONE_INTERVAL_MS;
    g_knock_next_check = SDL_GetTicks() + 30000;
	
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
        if (g_fx_buf) {
            SDL_FreeWAV(g_fx_buf);
            g_fx_buf = NULL;
            g_fx     = NULL;
        }
        if (g_phone_buf) {
            SDL_FreeWAV(g_phone_buf);
            g_phone_buf = NULL;
            g_phone     = NULL;
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
    if (g_fx_buf) {
        SDL_FreeWAV(g_fx_buf);
        g_fx_buf = NULL;
        g_fx     = NULL;
    }
    if (g_phone_buf) {
        SDL_FreeWAV(g_phone_buf);
        g_phone_buf = NULL;
        g_phone     = NULL;
    }
    if (g_knock_buf) {
        SDL_FreeWAV(g_knock_buf);
        g_knock_buf = NULL;
        g_knock = NULL;
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
        g_voice[slot].pos    = 0.0;
        g_voice[slot].gain   = 0.0f;
        g_voice[slot].active = 1;
    }
    SDL_UnlockAudioDevice(g_dev);
}

void audio_listener(double x, double z, int knock_eligible)
{
    double d, t;
    int v;

    if (!g_dev)
        return;

    SDL_LockAudioDevice(g_dev);
    if ((Sint32)(SDL_GetTicks() - g_knock_next_check) >= 0) {
        g_knock_next_check = SDL_GetTicks() + 30000;
        if (g_knock && knock_eligible && phone_random() % 100 < 25) {
            g_knock_pos = 0.0;
            g_knock_active = 1;
        }
    }
if (g_phone &&
        (Sint32)(SDL_GetTicks() - g_phone_next_check) >= 0) {
        g_phone_next_check = SDL_GetTicks() + PHONE_INTERVAL_MS;

			if (!g_phone_active && phone_random() % 100 < 25) {
				double angle = (double)phone_random() / 4294967295.0 * 2.0 * M_PI;
				double distance = 20.0 + (double)phone_random() / 4294967295.0 * 8.0;

            g_phone_x = x + cos(angle) * distance;
            g_phone_z = z + sin(angle) * distance;
            g_phone_pos = 0.0;
            g_phone_rings_left = PHONE_MIN_RINGS +
				phone_random() % (PHONE_MAX_RINGS - PHONE_MIN_RINGS + 1);
            g_phone_gain = 0.0f;
            g_phone_active = 1;
        }
    }
    for (v = 0; v < MAX_VOICES; v++) {
        if (!g_voice[v].active)
            continue;
        d = hypot(g_voice[v].x - x, g_voice[v].z - z);
        t = 1.0 - d / FLICKER_RANGE;
        if (t < 0.0)
            t = 0.0;
        g_voice[v].gain = (float)(t * t) * g_fx_scale;
    }
    if (g_phone_active) {
        d = hypot(g_phone_x - x, g_phone_z - z);
        t = 1.0 - d / PHONE_RANGE;
        if (t < 0.0)
            t = 0.0;
        g_phone_gain = (float)(t * t) * g_phone_scale;
    }
    SDL_UnlockAudioDevice(g_dev);
}