#ifndef LIGHT_H
#define LIGHT_H

#include "gl.h"

/* shaders.h LIGHT_GLSL uses these values as literals: keep in sync */
#define LIGHT_TEX_SIZE  32      /* panels per side of the light texture */
#define PANEL_SPACING   4.0f    /* metres between panels; divides CHUNK_SIZE */
#define PANEL_OFFSET    1.0f    /* panel centre offset from grid origin */

void   light_init(int panels, int flicker);
void   light_update(double cam_x, double cam_z, float time,
                    float *origin_x, float *origin_z);
void   light_free(void);

#endif
