#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl.h"
#include "mesh.h"

#define WALL_THICK   0.10f
#define WALL_HALF    (WALL_THICK * 0.5f)
#define WALL_EXT     (WALL_HALF - 0.002f)
#define PILLAR_HALF  0.25f

struct builder {
    float    *v;
    uint16_t *idx;
    int       nv, ni;
    int       cap_v, cap_i;
};

static void reserve(struct builder *b, int verts, int inds)
{
    if (b->nv + verts > b->cap_v) {
        b->cap_v = (b->nv + verts) * 2;
        b->v = realloc(b->v, (size_t)b->cap_v * VERTEX_FLOATS * sizeof(float));
    }
    if (b->ni + inds > b->cap_i) {
        b->cap_i = (b->ni + inds) * 2;
        b->idx = realloc(b->idx, (size_t)b->cap_i * sizeof(uint16_t));
    }
    if (!b->v || !b->idx) {
        printf("%-14s %s\n", "MESH ERROR", "out of memory");
        exit(1);
    }
}

/* vertical face from bottom edge (ax,az)-(bx,bz) up to the ceiling */
static void face(struct builder *b, float ax, float az, float bx, float bz,
                 float u0, float u1, float nx, float nz)
{
    const float y0 = 0.0f, y1 = CEIL_HEIGHT;
    float vert[4][VERTEX_FLOATS] = {
        { ax, y0, az, u0, y0, nx, nz },
        { bx, y0, bz, u1, y0, nx, nz },
        { bx, y1, bz, u1, y1, nx, nz },
        { ax, y1, az, u0, y1, nx, nz },
    };
    uint16_t base;

    reserve(b, 4, 6);
    base = (uint16_t)b->nv;
    memcpy(b->v + (size_t)b->nv * VERTEX_FLOATS, vert, sizeof(vert));
    b->nv += 4;

    b->idx[b->ni++] = base;
    b->idx[b->ni++] = base + 1;
    b->idx[b->ni++] = base + 2;
    b->idx[b->ni++] = base;
    b->idx[b->ni++] = base + 2;
    b->idx[b->ni++] = base + 3;
}

static void box(struct builder *b, float x0, float z0, float x1, float z1)
{
    face(b, x0, z0, x0, z1, z0, z1, -1.0f,  0.0f);   /* west  */
    face(b, x1, z0, x1, z1, z0, z1,  1.0f,  0.0f);   /* east  */
    face(b, x0, z0, x1, z0, x0, x1,  0.0f, -1.0f);   /* north */
    face(b, x0, z1, x1, z1, x0, x1,  0.0f,  1.0f);   /* south */
}

void mesh_build(struct chunk *c)
{
    struct builder b;
    int x, z, start;
    float l;

    memset(&b, 0, sizeof(b));

    /* west-edge walls: runs along z on line x */
    for (x = 0; x < CHUNK_CELLS; x++) {
        l = x * CELL_SIZE;
        z = 0;
        while (z < CHUNK_CELLS) {
            if (!c->wall_w[z][x]) {
                z++;
                continue;
            }
            start = z;
            while (z < CHUNK_CELLS && c->wall_w[z][x])
                z++;
            box(&b, l - WALL_HALF, start * CELL_SIZE - WALL_EXT,
                    l + WALL_HALF, z * CELL_SIZE + WALL_EXT);
        }
    }

    /* north-edge walls: runs along x on line z */
    for (z = 0; z < CHUNK_CELLS; z++) {
        l = z * CELL_SIZE;
        x = 0;
        while (x < CHUNK_CELLS) {
            if (!c->wall_n[z][x]) {
                x++;
                continue;
            }
            start = x;
            while (x < CHUNK_CELLS && c->wall_n[z][x])
                x++;
            box(&b, start * CELL_SIZE - WALL_EXT, l - WALL_HALF,
                    x * CELL_SIZE + WALL_EXT,     l + WALL_HALF);
        }
    }

    /* pillars on cell north-west corners */
    for (z = 0; z < CHUNK_CELLS; z++) {
        for (x = 0; x < CHUNK_CELLS; x++) {
            if (!c->pillar[z][x])
                continue;
            box(&b, x * CELL_SIZE - PILLAR_HALF, z * CELL_SIZE - PILLAR_HALF,
                    x * CELL_SIZE + PILLAR_HALF, z * CELL_SIZE + PILLAR_HALF);
        }
    }

    c->vbo = c->ibo = 0;
    c->index_count = b.ni;

    if (b.ni) {
        gl.GenBuffers(1, &c->vbo);
        gl.BindBuffer(GL_ARRAY_BUFFER, c->vbo);
        gl.BufferData(GL_ARRAY_BUFFER,
                      (GLsizeiptr)b.nv * VERTEX_FLOATS * sizeof(float),
                      b.v, GL_STATIC_DRAW);

        gl.GenBuffers(1, &c->ibo);
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, c->ibo);
        gl.BufferData(GL_ELEMENT_ARRAY_BUFFER,
                      (GLsizeiptr)b.ni * sizeof(uint16_t),
                      b.idx, GL_STATIC_DRAW);
    }

    free(b.v);
    free(b.idx);
}

void mesh_free(struct chunk *c)
{
    if (c->vbo)
        gl.DeleteBuffers(1, &c->vbo);
    if (c->ibo)
        gl.DeleteBuffers(1, &c->ibo);
    c->vbo = c->ibo = 0;
    c->index_count = 0;
}
