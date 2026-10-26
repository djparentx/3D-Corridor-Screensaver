#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "hash.h"
#include "light.h"
#include "world.h"

#define PANELS_PER_CHUNK  ((int32_t)(CHUNK_SIZE / PANEL_SPACING))
#define TRIG_SLOTS        16

enum { PANEL_ON, PANEL_DEAD, PANEL_FLICKER };

struct trig {
    int     used;
    int32_t i, j, slow;
};

static GLuint      g_tex;
static uint8_t     g_buf[LIGHT_TEX_SIZE * LIGHT_TEX_SIZE];
static uint32_t    g_dead_max    = 82u;
static uint32_t    g_flicker_max = 123u;
static float       g_bright      = 1.0f;
static struct trig g_trig[TRIG_SLOTS];
static int         g_trig_next;

static uint32_t panel_hash(int32_t i, int32_t j)
{
    return hash_2i(i, j, 0x51ED27u);
}

static int panel_kind(uint32_t h)
{
    uint32_t r = h & 1023u;

    if (r < g_dead_max)
        return PANEL_DEAD;
    if (r < g_flicker_max)
        return PANEL_FLICKER;
    return PANEL_ON;
}

static int32_t slow_index(float time)
{
    return (int32_t)floorf(time * 0.5f);
}

/* flickering panels stutter only in random 2 s burst windows */
static int burst_active(uint32_t h, int32_t slow)
{
    return (hash_2i((int32_t)h, slow, 0x9B0568u) & 3u) == 0u;
}

static int stutter_off(uint32_t h, float time)
{
    int32_t fast = (int32_t)floorf(time * 14.0f);

    return (hash_2i((int32_t)h, fast, 0x3C6EF3u) & 255u) < 56u;
}

static float panel_level(int32_t i, int32_t j, float time)
{
    uint32_t h = panel_hash(i, j);

    switch (panel_kind(h)) {
    case PANEL_DEAD:
        return 0.10f;
    case PANEL_FLICKER:
        if (!burst_active(h, slow_index(time)))
            return 1.0f;
        return stutter_off(h, time) ? 0.18f : 1.0f;
    default:
        return 0.88f + 0.12f * (float)((h >> 10) & 255u) / 255.0f;
    }
}

/* call with the light texture unit active */
void light_init(int panels, int flicker, int brightness)
{
    g_dead_max    = 82u * (uint32_t)panels;
    g_flicker_max = g_dead_max + 41u * (uint32_t)flicker;
    g_bright      = 0.25f + 0.75f * (float)brightness / 100.0f;

    gl.GenTextures(1, &g_tex);
    gl.BindTexture(GL_TEXTURE_2D, g_tex);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, LIGHT_TEX_SIZE, LIGHT_TEX_SIZE,
                  0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

/* call with the light texture unit active; origin is in panel units,
   relative to the camera chunk's panel grid */
void light_update(double cam_x, double cam_z, float time,
                  float *origin_x, float *origin_z)
{
    int32_t ccx = (int32_t)floor(cam_x / CHUNK_SIZE);
    int32_t ccz = (int32_t)floor(cam_z / CHUNK_SIZE);
    int32_t oi  = (int32_t)floor((cam_x - PANEL_OFFSET) / PANEL_SPACING) - LIGHT_TEX_SIZE / 2;
    int32_t oj  = (int32_t)floor((cam_z - PANEL_OFFSET) / PANEL_SPACING) - LIGHT_TEX_SIZE / 2;
    int x, z;

    for (z = 0; z < LIGHT_TEX_SIZE; z++)
        for (x = 0; x < LIGHT_TEX_SIZE; x++)
            g_buf[z * LIGHT_TEX_SIZE + x] =
                (uint8_t)(panel_level(oi + x, oj + z, time) * g_bright * 255.0f + 0.5f);

    gl.BindTexture(GL_TEXTURE_2D, g_tex);
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LIGHT_TEX_SIZE, LIGHT_TEX_SIZE,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, g_buf);

    *origin_x = (float)(oi - ccx * PANELS_PER_CHUNK);
    *origin_z = (float)(oj - ccz * PANELS_PER_CHUNK);
}

/* panels within range whose burst just produced its first stutter */
int light_flicker_events(double cam_x, double cam_z, float time, double range,
                         struct flicker_event *out, int max)
{
    int32_t ci    = (int32_t)floor((cam_x - PANEL_OFFSET) / PANEL_SPACING);
    int32_t cj    = (int32_t)floor((cam_z - PANEL_OFFSET) / PANEL_SPACING);
    int32_t reach = (int32_t)ceil(range / PANEL_SPACING);
    int32_t slow  = slow_index(time);
    int32_t i, j;
    uint32_t h;
    double px, pz;
    int n = 0, k, seen;

    for (j = cj - reach; j <= cj + reach + 1; j++) {
        for (i = ci - reach; i <= ci + reach + 1; i++) {
            h = panel_hash(i, j);
            if (panel_kind(h) != PANEL_FLICKER)
                continue;
            if (!burst_active(h, slow) || !stutter_off(h, time))
                continue;

            px = i * (double)PANEL_SPACING + PANEL_OFFSET;
            pz = j * (double)PANEL_SPACING + PANEL_OFFSET;
            if (hypot(px - cam_x, pz - cam_z) > range)
                continue;

            seen = 0;
            for (k = 0; k < TRIG_SLOTS; k++) {
                if (g_trig[k].used && g_trig[k].i == i &&
                    g_trig[k].j == j && g_trig[k].slow == slow) {
                    seen = 1;
                    break;
                }
            }
            if (seen)
                continue;

            g_trig[g_trig_next].used = 1;
            g_trig[g_trig_next].i    = i;
            g_trig[g_trig_next].j    = j;
            g_trig[g_trig_next].slow = slow;
            g_trig_next = (g_trig_next + 1) % TRIG_SLOTS;

            if (n < max) {
                out[n].i = i;
                out[n].j = j;
                out[n].x = px;
                out[n].z = pz;
                n++;
            }
        }
    }
    return n;
}

void light_free(void)
{
    gl.DeleteTextures(1, &g_tex);
}