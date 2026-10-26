#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl.h"
#include "hash.h"
#include "mesh.h"

#define WALL_THICK   0.10f
#define WALL_HALF    (WALL_THICK * 0.5f)
#define WALL_EXT     (WALL_HALF - 0.002f)
#define PILLAR_HALF  0.25f

struct builder {
    float    *v;
    int       nv, cap_v;
    uint16_t *idx[WALL_VARIANTS];
    int       ni[WALL_VARIANTS];
    int       cap_i[WALL_VARIANTS];
    uint32_t  seed;
    uint32_t  counter;
};

static void *grow(void *p, int *cap, int need, size_t elem)
{
    if (need <= *cap)
        return p;
    *cap = need * 2;
    p = realloc(p, (size_t)*cap * elem);
    if (!p) {
        printf("%-14s %s\n", "MESH ERROR", "out of memory");
        exit(1);
    }
    return p;
}

/* stable per-face stain variant */
static int next_variant(struct builder *b)
{
    return (int)(hash_u32(b->seed ^ (b->counter++ * 0x9E3779B9u)) % WALL_VARIANTS);
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
    int var = next_variant(b);
    uint16_t base, *ix;

    b->v = grow(b->v, &b->cap_v, b->nv + 4, VERTEX_FLOATS * sizeof(float));
    b->idx[var] = grow(b->idx[var], &b->cap_i[var], b->ni[var] + 6, sizeof(uint16_t));

    base = (uint16_t)b->nv;
    memcpy(b->v + (size_t)b->nv * VERTEX_FLOATS, vert, sizeof(vert));
    b->nv += 4;

    ix = b->idx[var] + b->ni[var];
    ix[0] = base;
    ix[1] = base + 1;
    ix[2] = base + 2;
    ix[3] = base;
    ix[4] = base + 2;
    ix[5] = base + 3;
    b->ni[var] += 6;
}

/* wall along z on x line l, cells [start, end); long faces split per cell */
static void run_z(struct builder *b, float l, int start, int end)
{
    float x0 = l - WALL_HALF, x1 = l + WALL_HALF;
    float za, zb;
    int k;

    for (k = start; k < end; k++) {
        za = k * CELL_SIZE       - (k == start   ? WALL_EXT : 0.0f);
        zb = (k + 1) * CELL_SIZE + (k == end - 1 ? WALL_EXT : 0.0f);
        face(b, x0, za, x0, zb, za, zb, -1.0f, 0.0f);
        face(b, x1, za, x1, zb, za, zb,  1.0f, 0.0f);
    }

    za = start * CELL_SIZE - WALL_EXT;
    zb = end * CELL_SIZE + WALL_EXT;
    face(b, x0, za, x1, za, x0, x1, 0.0f, -1.0f);
    face(b, x0, zb, x1, zb, x0, x1, 0.0f,  1.0f);
}

/* wall along x on z line l, cells [start, end); long faces split per cell */
static void run_x(struct builder *b, float l, int start, int end)
{
    float z0 = l - WALL_HALF, z1 = l + WALL_HALF;
    float xa, xb;
    int k;

    for (k = start; k < end; k++) {
        xa = k * CELL_SIZE       - (k == start   ? WALL_EXT : 0.0f);
        xb = (k + 1) * CELL_SIZE + (k == end - 1 ? WALL_EXT : 0.0f);
        face(b, xa, z0, xb, z0, xa, xb, 0.0f, -1.0f);
        face(b, xa, z1, xb, z1, xa, xb, 0.0f,  1.0f);
    }

    xa = start * CELL_SIZE - WALL_EXT;
    xb = end * CELL_SIZE + WALL_EXT;
    face(b, xa, z0, xa, z1, z0, z1, -1.0f, 0.0f);
    face(b, xb, z0, xb, z1, z0, z1,  1.0f, 0.0f);
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
    uint16_t *all;
    int x, z, v, start, total;

    memset(&b, 0, sizeof(b));
    b.seed = hash_2i(c->cx, c->cz, 0xB5297A4Du);

    /* west-edge walls: runs along z on line x */
    for (x = 0; x < CHUNK_CELLS; x++) {
        z = 0;
        while (z < CHUNK_CELLS) {
            if (!c->wall_w[z][x]) {
                z++;
                continue;
            }
            start = z;
            while (z < CHUNK_CELLS && c->wall_w[z][x])
                z++;
            run_z(&b, x * CELL_SIZE, start, z);
        }
    }

    /* north-edge walls: runs along x on line z */
    for (z = 0; z < CHUNK_CELLS; z++) {
        x = 0;
        while (x < CHUNK_CELLS) {
            if (!c->wall_n[z][x]) {
                x++;
                continue;
            }
            start = x;
            while (x < CHUNK_CELLS && c->wall_n[z][x])
                x++;
            run_x(&b, z * CELL_SIZE, start, x);
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

    total = 0;
    for (v = 0; v < WALL_VARIANTS; v++) {
        c->idx_start[v] = total;
        c->idx_count[v] = b.ni[v];
        total += b.ni[v];
    }

    c->vbo = c->ibo = 0;

    if (total) {
        all = malloc((size_t)total * sizeof(uint16_t));
        if (!all) {
            printf("%-14s %s\n", "MESH ERROR", "out of memory");
            exit(1);
        }
        for (v = 0; v < WALL_VARIANTS; v++)
            if (b.ni[v])
                memcpy(all + c->idx_start[v], b.idx[v],
                       (size_t)b.ni[v] * sizeof(uint16_t));

        gl.GenBuffers(1, &c->vbo);
        gl.BindBuffer(GL_ARRAY_BUFFER, c->vbo);
        gl.BufferData(GL_ARRAY_BUFFER,
                      (GLsizeiptr)b.nv * VERTEX_FLOATS * sizeof(float),
                      b.v, GL_STATIC_DRAW);

        gl.GenBuffers(1, &c->ibo);
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, c->ibo);
        gl.BufferData(GL_ELEMENT_ARRAY_BUFFER,
                      (GLsizeiptr)total * sizeof(uint16_t),
                      all, GL_STATIC_DRAW);
        free(all);
    }

    free(b.v);
    for (v = 0; v < WALL_VARIANTS; v++)
        free(b.idx[v]);
}

void mesh_free(struct chunk *c)
{
    int v;

    if (c->vbo)
        gl.DeleteBuffers(1, &c->vbo);
    if (c->ibo)
        gl.DeleteBuffers(1, &c->ibo);
    c->vbo = c->ibo = 0;
    for (v = 0; v < WALL_VARIANTS; v++) {
        c->idx_start[v] = 0;
        c->idx_count[v] = 0;
    }
}