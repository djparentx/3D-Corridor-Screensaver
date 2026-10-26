#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "world.h"
#include "mesh.h"

enum { BORDER_OPEN, BORDER_WALLED };

struct gen_params {
    int min_room;
    int gap_min, gap_max;
    int gaps_min, gaps_max;
    int skip_pct;
    int border;
    int pillar_pct;
};

static const struct gen_params g_params[CHUNK_TYPES] = {
    /* min  gap    gaps   skip  border         pillar */
    {  6,   2, 4,  1, 2,  40,  BORDER_OPEN,    4 },   /* CHUNK_OPEN    */
    {  3,   1, 1,  1, 1,  15,  BORDER_WALLED,  0 },   /* CHUNK_OFFICE  */
    {  1,   1, 1,  1, 1,   0,  BORDER_WALLED,  0 },   /* CHUNK_MAZE    */
    { 99,   0, 0,  0, 0,   0,  BORDER_OPEN,    0 },   /* CHUNK_PILLARS */
};

static struct chunk g_slots[CHUNK_SLOTS];
static uint32_t     g_seed;
static uint32_t     g_rng;
static int32_t      g_center_x, g_center_z;
static int          g_have_center;

static uint32_t hash3(int32_t x, int32_t z, uint32_t s)
{
    uint32_t h = s ^ ((uint32_t)x * 0x9E3779B1u) ^ ((uint32_t)z * 0x85EBCA77u);

    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

static uint32_t rnd(void)
{
    uint32_t x = g_rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;
    return x;
}

static int rnd_range(int n)
{
    return n <= 1 ? 0 : (int)(rnd() % (uint32_t)n);
}

/* vertical: line = x index, pos = z index (wall_w)
   horizontal: line = z index, pos = x index (wall_n) */
static void set_edge(struct chunk *c, int vertical, int line, int pos, uint8_t v)
{
    if (vertical)
        c->wall_w[pos][line] = v;
    else
        c->wall_n[line][pos] = v;
}

static void carve_gaps(struct chunk *c, int vertical, int line, int from,
                       int len, int count, int gmin, int gmax)
{
    int g, k, gw, start;

    for (g = 0; g < count; g++) {
        gw = gmin + rnd_range(gmax - gmin + 1);
        if (gw > len)
            gw = len;
        start = from + rnd_range(len - gw + 1);
        for (k = 0; k < gw; k++)
            set_edge(c, vertical, line, start + k, 0);
    }
}

static void wall_line(struct chunk *c, const struct gen_params *p,
                      int vertical, int line, int from, int len)
{
    int i;

    for (i = 0; i < len; i++)
        set_edge(c, vertical, line, from + i, 1);

    carve_gaps(c, vertical, line, from, len,
               p->gaps_min + rnd_range(p->gaps_max - p->gaps_min + 1),
               p->gap_min, p->gap_max);
}

static void divide(struct chunk *c, const struct gen_params *p,
                   int x0, int z0, int w, int h)
{
    int vertical, sx, sz;

    if (w < 2 && h < 2)
        return;
    if (w <= p->min_room && h <= p->min_room)
        return;
    if (p->skip_pct && w <= p->min_room * 2 && h <= p->min_room * 2 &&
        rnd_range(100) < p->skip_pct)
        return;

    if (w > h)
        vertical = 1;
    else if (h > w)
        vertical = 0;
    else
        vertical = (int)(rnd() & 1);

    if (vertical && w < 2)
        vertical = 0;
    if (!vertical && h < 2)
        vertical = 1;

    if (vertical) {
        sx = x0 + 1 + rnd_range(w - 1);
        wall_line(c, p, 1, sx, z0, h);
        divide(c, p, x0, z0, sx - x0, h);
        divide(c, p, sx, z0, x0 + w - sx, h);
    } else {
        sz = z0 + 1 + rnd_range(h - 1);
        wall_line(c, p, 0, sz, x0, w);
        divide(c, p, x0, z0, w, sz - z0);
        divide(c, p, x0, sz, w, z0 + h - sz);
    }
}

/* chunk owns its west (x=0) and north (z=0) border lines */
static void build_border(struct chunk *c, int vertical, const struct gen_params *p)
{
    int i, s, segs, len, start;

    if (p->border == BORDER_WALLED) {
        for (i = 0; i < CHUNK_CELLS; i++)
            set_edge(c, vertical, 0, i, 1);
        carve_gaps(c, vertical, 0, 0, CHUNK_CELLS, 2 + rnd_range(3), 1, 2);
        return;
    }

    /* max 2 segments of max 6 cells: border always keeps a gap */
    segs = rnd_range(3);
    for (s = 0; s < segs; s++) {
        len   = 2 + rnd_range(5);
        start = rnd_range(CHUNK_CELLS - len + 1);
        for (i = 0; i < len; i++)
            set_edge(c, vertical, 0, start + i, 1);
    }
}

static void generate(struct chunk *c, int32_t cx, int32_t cz)
{
    const struct gen_params *p;
    int r, x, z;

    memset(c, 0, sizeof(*c));
    c->cx = cx;
    c->cz = cz;
    g_rng = hash3(cx, cz, g_seed) | 1u;

    r = rnd_range(100);
    c->type = r < 35 ? CHUNK_OPEN :
              r < 65 ? CHUNK_OFFICE :
              r < 80 ? CHUNK_MAZE : CHUNK_PILLARS;
    p = &g_params[c->type];

    build_border(c, 1, p);
    build_border(c, 0, p);
    divide(c, p, 0, 0, CHUNK_CELLS, CHUNK_CELLS);

    for (z = 0; z < CHUNK_CELLS; z++) {
        for (x = 0; x < CHUNK_CELLS; x++) {
            if (c->type == CHUNK_PILLARS)
                c->pillar[z][x] = !(x & 1) && !(z & 1);
            else if (p->pillar_pct && rnd_range(100) < p->pillar_pct)
                c->pillar[z][x] = 1;
        }
    }

    c->valid = 1;
}

static struct chunk *find_chunk(int32_t cx, int32_t cz)
{
    int i;

    for (i = 0; i < CHUNK_SLOTS; i++)
        if (g_slots[i].valid && g_slots[i].cx == cx && g_slots[i].cz == cz)
            return &g_slots[i];
    return NULL;
}

static struct chunk *free_slot(void)
{
    int i;

    for (i = 0; i < CHUNK_SLOTS; i++)
        if (!g_slots[i].valid)
            return &g_slots[i];
    return NULL;
}

void world_init(uint32_t seed)
{
    g_seed = seed;
    g_have_center = 0;
    memset(g_slots, 0, sizeof(g_slots));
}

void world_update(double cam_x, double cam_z)
{
    int32_t ccx = (int32_t)floor(cam_x / CHUNK_SIZE);
    int32_t ccz = (int32_t)floor(cam_z / CHUNK_SIZE);
    int32_t dx, dz;
    struct chunk *c;
    int i;

    if (g_have_center && ccx == g_center_x && ccz == g_center_z)
        return;

    g_have_center = 1;
    g_center_x = ccx;
    g_center_z = ccz;

    for (i = 0; i < CHUNK_SLOTS; i++) {
        c = &g_slots[i];
        if (c->valid && (abs(c->cx - ccx) > CHUNK_RADIUS ||
                         abs(c->cz - ccz) > CHUNK_RADIUS)) {
            mesh_free(c);
            c->valid = 0;
        }
    }

    for (dz = -CHUNK_RADIUS; dz <= CHUNK_RADIUS; dz++) {
        for (dx = -CHUNK_RADIUS; dx <= CHUNK_RADIUS; dx++) {
            if (find_chunk(ccx + dx, ccz + dz))
                continue;
            c = free_slot();
            if (!c)
                return;
            generate(c, ccx + dx, ccz + dz);
            mesh_build(c);
        }
    }
}

struct chunk *world_chunks(void)
{
    return g_slots;
}

void world_free(void)
{
    int i;

    for (i = 0; i < CHUNK_SLOTS; i++) {
        if (g_slots[i].valid)
            mesh_free(&g_slots[i]);
        g_slots[i].valid = 0;
    }
}
