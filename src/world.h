#ifndef WORLD_H
#define WORLD_H

#include <stdint.h>

#define CELL_SIZE     2.0f
#define CEIL_HEIGHT   2.7f
#define CHUNK_CELLS   16
#define CHUNK_SIZE    (CELL_SIZE * CHUNK_CELLS)
#define CHUNK_RADIUS  2
#define CHUNK_SPAN    (CHUNK_RADIUS * 2 + 1)
#define CHUNK_SLOTS   (CHUNK_SPAN * CHUNK_SPAN)

enum chunk_type {
    CHUNK_OPEN,
    CHUNK_OFFICE,
    CHUNK_MAZE,
    CHUNK_PILLARS,
    CHUNK_TYPES
};

struct chunk {
    int          valid;
    int32_t      cx, cz;
    int          type;
    /* [z][x]: wall on the west edge of cell (x,z) */
    uint8_t      wall_w[CHUNK_CELLS][CHUNK_CELLS];
    /* [z][x]: wall on the north edge of cell (x,z) */
    uint8_t      wall_n[CHUNK_CELLS][CHUNK_CELLS];
    /* [z][x]: pillar on the north-west corner of cell (x,z) */
    uint8_t      pillar[CHUNK_CELLS][CHUNK_CELLS];
    unsigned int vbo, ibo;
    int          index_count;
};

void          world_init(uint32_t seed);
void          world_update(double cam_x, double cam_z);
struct chunk *world_chunks(void);
void          world_free(void);

#endif
