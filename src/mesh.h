#ifndef MESH_H
#define MESH_H

#include "world.h"

/* x y z  u v  nx nz */
#define VERTEX_FLOATS 7

void mesh_build(struct chunk *c);
void mesh_free(struct chunk *c);

#endif
