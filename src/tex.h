#ifndef TEX_H
#define TEX_H

#include "gl.h"
#include "world.h"

/* world size of one texture repeat; must divide CHUNK_SIZE */
#define WALL_TEX_WIDTH   CELL_SIZE
#define FLOOR_TEX_SIZE   2.0f

struct textures {
    GLuint wall[WALL_VARIANTS];
    GLuint carpet;
    GLuint ceiling;
};

void tex_create(struct textures *t);
void tex_destroy(struct textures *t);

#endif