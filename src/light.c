#include <math.h>
#include <stdint.h>
#include "hash.h"
#include "light.h"
#include "world.h"

#define PANELS_PER_CHUNK  ((int32_t)(CHUNK_SIZE / PANEL_SPACING))

static GLuint  g_tex;
static uint8_t g_buf[LIGHT_TEX_SIZE * LIGHT_TEX_SIZE];

static float panel_level(int32_t i, int32_t j, float time)
{
    uint32_t h = hash_2i(i, j, 0x51ED27u);
    uint32_t r = h & 1023u;
    int32_t  slow, fast;

    /* ~8% dead */
    if (r < 82u)
        return 0.10f;

    /* ~4% flickering: stutters in random 2 s bursts */
    if (r < 123u) {
        slow = (int32_t)floorf(time * 0.5f);
        if ((hash_2i((int32_t)h, slow, 0x9B0568u) & 3u) != 0u)
            return 1.0f;
        fast = (int32_t)floorf(time * 14.0f);
        return (hash_2i((int32_t)h, fast, 0x3C6EF3u) & 255u) < 56u ? 0.18f : 1.0f;
    }

    /* normal: slight brightness variation */
    return 0.88f + 0.12f * (float)((h >> 10) & 255u) / 255.0f;
}

/* call with the light texture unit active */
void light_init(void)
{
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
                (uint8_t)(panel_level(oi + x, oj + z, time) * 255.0f + 0.5f);

    gl.BindTexture(GL_TEXTURE_2D, g_tex);
    gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LIGHT_TEX_SIZE, LIGHT_TEX_SIZE,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, g_buf);

    *origin_x = (float)(oi - ccx * PANELS_PER_CHUNK);
    *origin_z = (float)(oj - ccz * PANELS_PER_CHUNK);
}

void light_free(void)
{
    gl.DeleteTextures(1, &g_tex);
}
