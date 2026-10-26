#ifndef TEX_H
#define TEX_H

#include "gl.h"

/* world size of one texture repeat; must divide CHUNK_SIZE */
#define WALL_TEX_WIDTH   1.0f
#define FLOOR_TEX_SIZE   2.0f

struct textures {
    GLuint wall;
    GLuint carpet;
    GLuint ceiling;
};

void tex_create(struct textures *t);
void tex_destroy(struct textures *t);

#endif
