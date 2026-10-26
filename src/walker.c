#include <math.h>
#include <stdint.h>
#include <string.h>
#include "walker.h"
#include "world.h"

#define GRID        (CHUNK_SPAN * CHUNK_CELLS)
#define TURN_RATE   3.0f
#define BOB_FREQ    1.8f
#define BOB_AMP     0.02f
#define TWO_PI      6.28318531f

static int16_t g_dist[GRID * GRID];
static int16_t g_parent[GRID * GRID];
static int16_t g_queue[GRID * GRID];

static const int g_dx[4] = { 0, 1, 0, -1 };   /* DIR_N, DIR_E, DIR_S, DIR_W */
static const int g_dz[4] = { -1, 0, 1, 0 };

static uint32_t rnd(struct walker *w)
{
    uint32_t x = w->rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w->rng = x;
    return x;
}

static int32_t floor_div(int32_t a, int32_t b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

static double cell_center(int32_t g)
{
    return g * (double)CELL_SIZE + CELL_SIZE * 0.5;
}

static void catmull(const double *p0, const double *p1, const double *p2,
                    const double *p3, double t, double *out, double *tan)
{
    double t2 = t * t, t3 = t2 * t;
    int k;

    for (k = 0; k < 2; k++) {
        double a = 2.0 * p1[k];
        double b = -p0[k] + p2[k];
        double c = 2.0 * p0[k] - 5.0 * p1[k] + 4.0 * p2[k] - p3[k];
        double d = -p0[k] + 3.0 * p1[k] - 3.0 * p2[k] + p3[k];

        out[k] = 0.5 * (a + b * t + c * t2 + d * t3);
        tan[k] = 0.5 * (b + 2.0 * c * t + 3.0 * d * t2);
    }
}

/* new path from cell centre (fx,fz); (px,pz) is the point it arrived from */
static void plan(struct walker *w, double fx, double fz, double px, double pz)
{
    int32_t sgx = (int32_t)floor(fx / CELL_SIZE);
    int32_t sgz = (int32_t)floor(fz / CELL_SIZE);
    int32_t ox  = (floor_div(sgx, CHUNK_CELLS) - CHUNK_RADIUS) * CHUNK_CELLS;
    int32_t oz  = (floor_div(sgz, CHUNK_CELLS) - CHUNK_RADIUS) * CHUNK_CELLS;
    double  hx  = fx - px, hz = fz - pz;
    int start, idx, nidx, d, lx, lz, nx, nz, count, k;
    int head = 0, tail = 0, best = -1, fallback = -1, far, preferred = 0, any = 0;

    memset(g_dist, 0xFF, sizeof(g_dist));

    start = (int)((sgz - oz) * GRID + (sgx - ox));
    far   = start;
    g_dist[start] = 0;
    g_queue[tail++] = (int16_t)start;

    while (head < tail) {
        idx = g_queue[head++];
        lx  = idx % GRID;
        lz  = idx / GRID;
        if (g_dist[idx] >= PATH_MAX)
            continue;

        for (d = 0; d < 4; d++) {
            nx = lx + g_dx[d];
            nz = lz + g_dz[d];
            if (nx < 0 || nz < 0 || nx >= GRID || nz >= GRID)
                continue;
            nidx = nz * GRID + nx;
            if (g_dist[nidx] >= 0)
                continue;
            if (!world_open(ox + lx, oz + lz, d))
                continue;

            g_dist[nidx]    = (int16_t)(g_dist[idx] + 1);
            g_parent[nidx]  = (int16_t)idx;
            g_queue[tail++] = (int16_t)nidx;

            if (g_dist[nidx] > g_dist[far])
                far = nidx;

            if (g_dist[nidx] >= PATH_MIN) {
                double rx = (double)(ox + nx - sgx);
                double rz = (double)(oz + nz - sgz);

                if (rx * hx + rz * hz > 0.0) {
                    preferred++;
                    if (rnd(w) % (uint32_t)preferred == 0u)
                        best = nidx;
                }
                any++;
                if (rnd(w) % (uint32_t)any == 0u)
                    fallback = nidx;
            }
        }
    }

    idx   = best >= 0 ? best : fallback >= 0 ? fallback : far;
    count = g_dist[idx] + 1;

    /* walk back from target to start */
    for (k = count; k >= 1; k--) {
        w->pts[k][0] = cell_center(ox + idx % GRID);
        w->pts[k][1] = cell_center(oz + idx / GRID);
        if (k > 1)
            idx = g_parent[idx];
    }

    if (count < 2) {
        w->pts[2][0] = w->pts[1][0];
        w->pts[2][1] = w->pts[1][1];
        count = 2;
    }

    w->n = count;
    w->pts[0][0] = px;
    w->pts[0][1] = pz;
    w->pts[count + 1][0] = 2.0 * w->pts[count][0] - w->pts[count - 1][0];
    w->pts[count + 1][1] = 2.0 * w->pts[count][1] - w->pts[count - 1][1];
    w->seg = 1;
}

/* world chunks around (x,z) must be loaded first */
void walker_init(struct walker *w, double x, double z, uint32_t seed)
{
    memset(w, 0, sizeof(*w));
    w->rng = seed * 2654435761u | 1u;
    w->x   = cell_center((int32_t)floor(x / CELL_SIZE));
    w->z   = cell_center((int32_t)floor(z / CELL_SIZE));
    w->yaw = 0.0f;

    /* initial heading -z (yaw 0) */
    plan(w, w->x, w->z, w->x, w->z + CELL_SIZE);
}

void walker_update(struct walker *w, float dt)
{
    double pos[2], tan[2], len;
    double *p1, *p2;
    float target, diff;
    int s, plans = 0;

    if (dt > 0.1f)
        dt = 0.1f;

    p1  = w->pts[w->seg];
    p2  = w->pts[w->seg + 1];
    len = hypot(p2[0] - p1[0], p2[1] - p1[1]);
    if (len < 0.01)
        len = 0.01;
    w->u += (float)(WALK_SPEED * dt / len);

    while (w->u >= 1.0f) {
        w->u -= 1.0f;
        w->seg++;
        if (w->seg >= w->n) {
            if (plans++) {
                w->seg = w->n - 1;
                w->u = 0.0f;
                break;
            }
            plan(w, w->pts[w->n][0], w->pts[w->n][1],
                    w->pts[w->n - 1][0], w->pts[w->n - 1][1]);
        }
    }

    s = w->seg;
    catmull(w->pts[s - 1], w->pts[s], w->pts[s + 1], w->pts[s + 2], w->u, pos, tan);
    w->x = pos[0];
    w->z = pos[1];

    if (tan[0] * tan[0] + tan[1] * tan[1] > 1e-6) {
        target = (float)atan2(-tan[0], -tan[1]);
        diff = target - w->yaw;
        while (diff >  (float)M_PI) diff -= TWO_PI;
        while (diff < -(float)M_PI) diff += TWO_PI;
        w->yaw += diff * fminf(1.0f, TURN_RATE * dt);
        while (w->yaw >  (float)M_PI) w->yaw -= TWO_PI;
        while (w->yaw < -(float)M_PI) w->yaw += TWO_PI;
    }

    w->bob_phase += dt * BOB_FREQ * TWO_PI;
    if (w->bob_phase > TWO_PI)
        w->bob_phase -= TWO_PI;
}

float walker_bob(const struct walker *w)
{
    return BOB_AMP * sinf(w->bob_phase);
}
