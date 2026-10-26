#ifndef LIGHT_H
#define LIGHT_H

#include <stdint.h>
#include "gl.h"

/* shaders.h LIGHT_GLSL uses these values as literals: keep in sync */
#define LIGHT_TEX_SIZE  32      /* panels per side of the light texture */
#define PANEL_SPACING   4.0f    /* metres between panels; divides CHUNK_SIZE */
#define PANEL_OFFSET    1.0f    /* panel centre offset from grid origin */

struct flicker_event {
    int32_t i, j;     /* global panel index */
    double  x, z;     /* panel centre, world */
};

void   light_init(int panels, int flicker, int brightness);
void   light_update(double cam_x, double cam_z, float time,
                    float *origin_x, float *origin_z);
int    light_flicker_events(double cam_x, double cam_z, float time, double range,
                            struct flicker_event *out, int max);
void   light_free(void);

#endif