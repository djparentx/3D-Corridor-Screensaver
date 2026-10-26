#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tex.h"

#define WALL_W   256    /* 1.0 m  */
#define WALL_H   512    /* 2.7 m  */
#define FLOOR_W  256    /* 2.0 m  */
#define CEIL_W   256    /* 2.0 m  */

static uint32_t ihash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static uint32_t hash2(int x, int y, uint32_t salt)
{
    return ihash((uint32_t)x * 73856093u ^ ihash((uint32_t)y * 19349663u ^ salt));
}

static float lattice(int x, int y, int per_x, int per_y, uint32_t salt)
{
    x = ((x % per_x) + per_x) % per_x;
    y = ((y % per_y) + per_y) % per_y;
    return (float)(hash2(x, y, salt) & 0xFFFF) / 65535.0f;
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

/* row 0 = floor level */
static void gen_wall(uint8_t *buf)
{
    const int base_px = 23;     /* 0.12 m baseboard */
    int x, y, cx, cy, dx;
    float n, stain, m, d, r, g, b;

    for (y = 0; y < WALL_H; y++) {
        for (x = 0; x < WALL_W; x++) {
            uint8_t *p = buf + (y * WALL_W + x) * 3;

            n = fbm((float)x, (float)y, 32, WALL_W, WALL_H, 4, 11u);

            if (y < base_px) {
                m = 0.92f + 0.12f * n;
                if (y >= base_px - 2)
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

            /* faint stains */
            stain = smoothstepf(0.60f, 0.74f,
                                fbm((float)x, (float)y, 128, WALL_W, WALL_H, 3, 23u));
            m *= 1.0f - 0.10f * stain;
            b *= 1.0f - 0.08f * stain;

            /* grime above baseboard, shadow below ceiling */
            if (y < base_px + 40)
                m *= 0.92f + 0.08f * (float)(y - base_px) / 40.0f;
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
            uint8_t *p = buf + (y * FLOOR_W + x) * 3;

            n      = fbm((float)x, (float)y, 64, FLOOR_W, FLOOR_W, 5, 37u);
            grain  = (float)(hash2(x, y, 41u) & 255) / 255.0f;
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
            uint8_t *p = buf + (y * CEIL_W + x) * 3;

            n = fbm((float)x, (float)y, 32, CEIL_W, CEIL_W, 3, 61u);

            /* 0.5 m tile grid */
            if (x % 64 < 2 || y % 64 < 2) {
                m = 0.97f + 0.03f * n;
                put(p, 0.70f * m, 0.69f * m, 0.62f * m);
                continue;
            }

            h = hash2(x, y, 67u) & 1023u;
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

    if (!buf) {
        printf("%-14s %s\n", "TEX ERROR", "out of memory");
        exit(1);
    }

    gen_wall(buf);
    t->wall = upload(buf, WALL_W, WALL_H, GL_CLAMP_TO_EDGE);

    gen_carpet(buf);
    t->carpet = upload(buf, FLOOR_W, FLOOR_W, GL_REPEAT);

    gen_ceiling(buf);
    t->ceiling = upload(buf, CEIL_W, CEIL_W, GL_REPEAT);

    free(buf);
}

void tex_destroy(struct textures *t)
{
    gl.DeleteTextures(1, &t->wall);
    gl.DeleteTextures(1, &t->carpet);
    gl.DeleteTextures(1, &t->ceiling);
}
