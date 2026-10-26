#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "hash.h"
#include "tex.h"

#define WALL_W   512    /* 2.0 m  */
#define WALL_H   512    /* 2.7 m  */
#define FLOOR_W  256    /* 2.0 m  */
#define CEIL_W   256    /* 2.0 m  */
#define BASE_PX  23     /* 0.12 m baseboard */

static float lattice(int x, int y, int per_x, int per_y, uint32_t salt)
{
    x = ((x % per_x) + per_x) % per_x;
    y = ((y % per_y) + per_y) % per_y;
    return (float)(hash_2i(x, y, salt) & 0xFFFF) / 65535.0f;
}

static float smoothstepf(float e0, float e1, float x)
{
    float t = (x - e0) / (e1 - e0);

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

/* tileable value noise; cell must divide w and h */
static float vnoise(float px, float py, int cell, int w, int h, uint32_t salt)
{
    int   per_x = w / cell, per_y = h / cell;
    float fx = px / (float)cell, fy = py / (float)cell;
    int   ix = (int)floorf(fx), iy = (int)floorf(fy);
    float tx = fx - (float)ix, ty = fy - (float)iy;
    float a, b, c, d, top, bot;

    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);

    a = lattice(ix,     iy,     per_x, per_y, salt);
    b = lattice(ix + 1, iy,     per_x, per_y, salt);
    c = lattice(ix,     iy + 1, per_x, per_y, salt);
    d = lattice(ix + 1, iy + 1, per_x, per_y, salt);

    top = a + (b - a) * tx;
    bot = c + (d - c) * tx;
    return top + (bot - top) * ty;
}

static float fbm(float px, float py, int cell, int w, int h, int octaves, uint32_t salt)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int o;

    for (o = 0; o < octaves && cell >= 1; o++) {
        sum  += amp * vnoise(px, py, cell, w, h, salt + (uint32_t)o * 1013u);
        norm += amp;
        amp  *= 0.5f;
        cell /= 2;
    }
    return sum / norm;
}

static uint8_t to8(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (uint8_t)(v * 255.0f + 0.5f);
}

static void put(uint8_t *p, float r, float g, float b)
{
    p[0] = to8(r);
    p[1] = to8(g);
    p[2] = to8(b);
}

static GLuint upload(const uint8_t *data, int w, int h, GLint wrap_t)
{
    GLuint id;

    gl.GenTextures(1, &id);
    gl.BindTexture(GL_TEXTURE_2D, id);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
    gl.GenerateMipmap(GL_TEXTURE_2D);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
    return id;
}

/* stain 0: faint blotches */
static float stain_blotch(int x, int y)
{
    float v = fbm((float)x, (float)y, 128, WALL_W, WALL_H, 3, 23u);

    return 0.12f * smoothstepf(0.60f, 0.74f, v);
}

/* stain 1: drips running down from the ceiling */
static float stain_drip(int x, int y)
{
    const int lane  = 16;
    const int lanes = WALL_W / lane;
    float best = 0.0f;
    int k;

    for (k = -1; k <= 1; k++) {
        int col = x / lane + k;
        uint32_t h;
        float cx, w, len, t, dist, core, halo, amt;

        if (col < 0 || col >= lanes)
            continue;

        h = hash_2i(col, 0, 71u);
        if ((h & 255u) >= 70u)
            continue;

        cx  = (float)(col * lane) + 4.0f + (float)((h >> 8) & 7u);
        cx += 1.5f * sinf((float)y * 0.045f + (float)(h & 63u));
        w   = 2.0f + (float)((h >> 11) & 3u);
        len = 90.0f + (float)((h >> 13) & 255u);
        t   = (float)(WALL_H - 1 - y) / len;
        if (t >= 1.0f)
            continue;

        dist = fabsf((float)x - cx);
        core = 1.0f - smoothstepf(0.0f, w, dist);
        halo = 1.0f - smoothstepf(w, w * 2.5f, dist);
        amt  = powf(1.0f - t, 1.4f) * (0.22f * core + 0.06f * halo);
        if (amt > best)
            best = amt;
    }
    return best;
}

/* stain 2: rising damp with a tide line */
static float stain_damp(int x, int y)
{
    float line = (float)BASE_PX + 55.0f
               + 75.0f * vnoise((float)x, 0.0f, 64, WALL_W, 64, 83u)
               + 10.0f * vnoise((float)x, 0.0f, 16, WALL_W, 16, 89u);
    float fy    = (float)y;
    float patch = fbm((float)x, (float)y, 64, WALL_W, WALL_H, 3, 97u);
    float below = 1.0f - smoothstepf(line - 6.0f, line, fy);
    float edge  = 1.0f - smoothstepf(0.0f, 3.5f, fabsf(fy - line));

    return (0.08f + 0.08f * patch) * below + 0.16f * edge;
}

/* row 0 = floor level */
static void gen_wall(uint8_t *buf, int variant)
{
    int x, y, cx, cy, dx;
    float n, m, d, r, g, b, edge, s;

    for (y = 0; y < WALL_H; y++) {
        for (x = 0; x < WALL_W; x++) {
            uint8_t *p = buf + ((size_t)y * WALL_W + x) * 3;

            n = fbm((float)x, (float)y, 32, WALL_W, WALL_H, 4, 11u);

            if (y < BASE_PX) {
                m = 0.92f + 0.12f * n;
                if (y >= BASE_PX - 2)
                    m *= 1.12f;
                put(p, 0.56f * m, 0.48f * m, 0.35f * m);
                continue;
            }

            r = 0.86f;
            g = 0.82f;
            b = 0.60f;
            m = 0.95f + 0.07f * n;

            /* thin vertical stripe every 0.25 m */
            cx = x % 64;
            if (cx < 2)
                m *= 0.88f;

            /* small diamond outline between stripes */
            cy = y % 48;
            dx = abs(cx - 32);
            d  = (float)dx + fabsf((float)cy - 24.0f) * 0.75f;
            if (d >= 7.0f && d <= 8.6f) {
                m *= 0.86f;
                b *= 0.97f;
            }

            /* stains fade out near segment edges so variants join cleanly */
            edge = smoothstepf(0.0f, 48.0f, (float)x) *
                   smoothstepf(0.0f, 48.0f, (float)(WALL_W - 1 - x));

            switch (variant) {
            case 0:  s = stain_blotch(x, y); break;
            case 1:  s = stain_drip(x, y);   break;
            default: s = stain_damp(x, y);   break;
            }
            s *= edge;
            m *= 1.0f - s;
            b *= 1.0f - 0.6f * s;

            /* grime above baseboard, shadow below ceiling */
            if (y < BASE_PX + 40)
                m *= 0.92f + 0.08f * (float)(y - BASE_PX) / 40.0f;
            if (y > WALL_H - 10)
                m *= 0.92f;

            put(p, r * m, g * m, b * m);
        }
    }
}

static void gen_carpet(uint8_t *buf)
{
    int x, y;
    float n, grain, blotch, m;

    for (y = 0; y < FLOOR_W; y++) {
        for (x = 0; x < FLOOR_W; x++) {
            uint8_t *p = buf + ((size_t)y * FLOOR_W + x) * 3;

            n      = fbm((float)x, (float)y, 64, FLOOR_W, FLOOR_W, 5, 37u);
            grain  = (float)(hash_2i(x, y, 41u) & 255u) / 255.0f;
            blotch = smoothstepf(0.58f, 0.72f,
                                 fbm((float)x, (float)y, 128, FLOOR_W, FLOOR_W, 3, 53u));

            m  = 0.80f + 0.30f * n + 0.10f * (grain - 0.5f);
            m *= 1.0f - 0.20f * blotch;

            put(p, 0.62f * m, 0.53f * m, 0.38f * m);
        }
    }
}

static void gen_ceiling(uint8_t *buf)
{
    int x, y;
    uint32_t h;
    float n, m;

    for (y = 0; y < CEIL_W; y++) {
        for (x = 0; x < CEIL_W; x++) {
            uint8_t *p = buf + ((size_t)y * CEIL_W + x) * 3;

            n = fbm((float)x, (float)y, 32, CEIL_W, CEIL_W, 3, 61u);

            /* 0.5 m tile grid */
            if (x % 64 < 2 || y % 64 < 2) {
                m = 0.97f + 0.03f * n;
                put(p, 0.70f * m, 0.69f * m, 0.62f * m);
                continue;
            }

            h = hash_2i(x, y, 67u) & 1023u;
            m = 0.95f + 0.06f * n;
            if (h < 40u)
                m *= 0.84f;
            else if (h < 120u)
                m *= 0.94f;

            put(p, 0.90f * m, 0.89f * m, 0.82f * m);
        }
    }
}

void tex_create(struct textures *t)
{
    uint8_t *buf = malloc((size_t)WALL_W * WALL_H * 3);
    int v;

    if (!buf) {
        printf("%-14s %s\n", "TEX ERROR", "out of memory");
        exit(1);
    }

    for (v = 0; v < WALL_VARIANTS; v++) {
        gen_wall(buf, v);
        t->wall[v] = upload(buf, WALL_W, WALL_H, GL_CLAMP_TO_EDGE);
    }

    gen_carpet(buf);
    t->carpet = upload(buf, FLOOR_W, FLOOR_W, GL_REPEAT);

    gen_ceiling(buf);
    t->ceiling = upload(buf, CEIL_W, CEIL_W, GL_REPEAT);

    free(buf);
}

void tex_destroy(struct textures *t)
{
    gl.DeleteTextures(WALL_VARIANTS, t->wall);
    gl.DeleteTextures(1, &t->carpet);
    gl.DeleteTextures(1, &t->ceiling);
}