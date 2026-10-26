#include <math.h>
#include <stdint.h>
#include <string.h>
#include "walker.h"
#include "world.h"

#define GRID        (CHUNK_SPAN * CHUNK_CELLS)
#define TURN_RATE   3.0f      /* yaw follow rate while walking */
#define TURN_TIME   0.8f      /* seconds per 90 degree turn */
#define STARE_TIME  1.5f
#define STOP_MIN    0.6       /* closer than this: stop at the following cell */
#define BOB_FREQ    1.8f
#define BOB_AMP     0.02f
#define TWO_PI      6.28318531f
#define HALF_PI     1.57079633f

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

static float wrap_pi(float a)
{
    while (a >  (float)M_PI) a -= TWO_PI;
    while (a < -(float)M_PI) a += TWO_PI;
    return a;
}

static float dir_yaw(int dir)
{
    return wrap_pi(-(float)dir * HALF_PI);
}

static int dir_of(double dx, double dz)
{
    if (fabs(dx) > fabs(dz))
        return dx > 0.0 ? DIR_E : DIR_W;
    return dz > 0.0 ? DIR_S : DIR_N;
}

/* smoothly turn the view toward a movement tangent */
static void face_tangent(struct walker *w, double tx, double tz, float dt)
{
    float diff;

    if (tx * tx + tz * tz < 1e-6)
        return;
    diff = wrap_pi((float)atan2(-tx, -tz) - w->yaw);
    w->yaw = wrap_pi(w->yaw + diff * fminf(1.0f, TURN_RATE * dt));
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

static void hermite(const struct walker *w, double s, double *pos, double *tan)
{
    double s2 = s * s, s3 = s2 * s;
    double h00 = 2.0 * s3 - 3.0 * s2 + 1.0;
    double h10 = s3 - 2.0 * s2 + s;
    double h01 = -2.0 * s3 + 3.0 * s2;
    double d00 = 6.0 * s2 - 6.0 * s;
    double d10 = 3.0 * s2 - 4.0 * s + 1.0;
    double d01 = -6.0 * s2 + 6.0 * s;
    int k;

    for (k = 0; k < 2; k++) {
        pos[k] = h00 * w->h0[k] + h10 * w->hm[k] + h01 * w->h1[k];
        tan[k] = d00 * w->h0[k] + d10 * w->hm[k] + d01 * w->h1[k];
    }
}

/* new path from cell centre (fx,fz); (px,pz) is the point it arrived from.
   force_dir >= 0 forces the first step in that direction. */
static void plan(struct walker *w, double fx, double fz, double px, double pz,
                 int force_dir)
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
            if (idx == start && force_dir >= 0 && d != force_dir)
                continue;
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

/* leave the stop cell from rest along 'facing' (force_dir) or freely (-1) */
static void depart(struct walker *w, int force_dir)
{
    double px = w->x - g_dx[w->facing] * (double)CELL_SIZE;
    double pz = w->z - g_dz[w->facing] * (double)CELL_SIZE;

    plan(w, w->x, w->z, px, pz, force_dir);

    /* P0 = P2: zero start tangent, eases in from rest */
    w->pts[0][0] = w->pts[2][0];
    w->pts[0][1] = w->pts[2][1];
    w->u = 0.0f;
    w->state = WALK_PATH;
}

static void begin_turn(struct walker *w)
{
    int sign = w->turn_q > 0 ? 1 : -1;

    w->turn_q  -= sign;
    w->facing   = (w->facing + sign + 4) % 4;
    w->yaw_from = w->yaw;
    w->yaw_to   = w->yaw + wrap_pi(dir_yaw(w->facing) - w->yaw);
    w->tt       = 0.0f;
    w->state    = WALK_TURNING;
}

/* stopped at a cell centre facing w->facing: turn, walk on, or stare */
static void next_action(struct walker *w)
{
    int32_t gx = (int32_t)floor(w->x / CELL_SIZE);
    int32_t gz = (int32_t)floor(w->z / CELL_SIZE);

    if (w->turn_q != 0) {
        begin_turn(w);
    } else if (world_open(gx, gz, w->facing)) {
        depart(w, w->facing);
    } else {
        w->stare = STARE_TIME;
        w->state = WALK_STARING;
    }
}

/* ease from the current position/velocity to a stop at the next cell centre */
static void begin_stop(struct walker *w)
{
    int     s = w->seg;
    double *a = w->pts[s], *b = w->pts[s + 1];
    double  dist, sp, dx, dz;

    dist = hypot(b[0] - w->x, b[1] - w->z);
    if (dist < STOP_MIN && s + 2 <= w->n) {
        a = w->pts[s + 1];
        b = w->pts[s + 2];
        dist = hypot(b[0] - w->x, b[1] - w->z);
    }

    w->facing = dir_of(b[0] - a[0], b[1] - a[1]);

    sp = hypot(w->vx, w->vz);
    if (sp > 0.05) {
        dx = w->vx / sp;
        dz = w->vz / sp;
    } else {
        dx = (b[0] - w->x) / (dist > 1e-6 ? dist : 1.0);
        dz = (b[1] - w->z) / (dist > 1e-6 ? dist : 1.0);
        sp = WALK_SPEED;
    }

    /* start tangent 1.5 * dist keeps the curve monotone (no overshoot) */
    w->hdur = (float)(1.5 * dist / sp);
    if (w->hdur < 0.3f)
        w->hdur = 0.3f;
    w->h0[0] = w->x;
    w->h0[1] = w->z;
    w->hm[0] = dx * 1.5 * dist;
    w->hm[1] = dz * 1.5 * dist;
    w->h1[0] = b[0];
    w->h1[1] = b[1];
    w->ht    = 0.0f;
    w->state = WALK_STOPPING;
}

/* world chunks around (x,z) must be loaded first */
void walker_init(struct walker *w, double x, double z, uint32_t seed)
{
    memset(w, 0, sizeof(*w));
    w->rng     = seed * 2654435761u | 1u;
    w->x       = cell_center((int32_t)floor(x / CELL_SIZE));
    w->z       = cell_center((int32_t)floor(z / CELL_SIZE));
    w->yaw     = 0.0f;
    w->facing  = DIR_N;
    w->state   = WALK_PATH;
    w->bob_amt = 1.0f;

    /* initial heading -z (yaw 0) */
    plan(w, w->x, w->z, w->x, w->z + CELL_SIZE, -1);
}

void walker_update(struct walker *w, float dt)
{
    double pos[2], tan[2], len;
    double *p1, *p2;
    float k, bob_target;
    int s, plans = 0;

    if (dt > 0.1f)
        dt = 0.1f;

    if (w->state == WALK_PATH && w->turn_q != 0)
        begin_stop(w);

    if (w->state == WALK_PATH) {
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
                        w->pts[w->n - 1][0], w->pts[w->n - 1][1], -1);
            }
        }

        s = w->seg;
        catmull(w->pts[s - 1], w->pts[s], w->pts[s + 1], w->pts[s + 2], w->u, pos, tan);
        w->x = pos[0];
        w->z = pos[1];

        len = hypot(w->pts[s + 1][0] - w->pts[s][0], w->pts[s + 1][1] - w->pts[s][1]);
        if (len < 0.01)
            len = 0.01;
        w->vx = tan[0] * WALK_SPEED / len;
        w->vz = tan[1] * WALK_SPEED / len;

        face_tangent(w, tan[0], tan[1], dt);

    } else if (w->state == WALK_STOPPING) {
        w->ht += dt / w->hdur;
        if (w->ht >= 1.0f) {
            w->x  = w->h1[0];
            w->z  = w->h1[1];
            w->vx = w->vz = 0.0;
            next_action(w);
        } else {
            hermite(w, w->ht, pos, tan);
            w->x = pos[0];
            w->z = pos[1];
            face_tangent(w, tan[0], tan[1], dt);
        }

    } else if (w->state == WALK_TURNING) {
        w->tt += dt / TURN_TIME;
        if (w->tt >= 1.0f) {
            w->yaw = dir_yaw(w->facing);
            next_action(w);
        } else {
            k = w->tt * w->tt * (3.0f - 2.0f * w->tt);
            w->yaw = wrap_pi(w->yaw_from + (w->yaw_to - w->yaw_from) * k);
        }

    } else { /* WALK_STARING */
        if (w->turn_q != 0) {
            begin_turn(w);
        } else {
            w->stare -= dt;
            if (w->stare <= 0.0f)
                depart(w, -1);
        }
    }

    bob_target = (w->state == WALK_PATH || w->state == WALK_STOPPING) ? 1.0f : 0.0f;
    w->bob_amt += (bob_target - w->bob_amt) * fminf(1.0f, 3.0f * dt);
    w->bob_phase += dt * BOB_FREQ * TWO_PI;
    if (w->bob_phase > TWO_PI)
        w->bob_phase -= TWO_PI;
}

void walker_turn(struct walker *w, int dir)
{
    w->turn_q += dir;
    if (w->turn_q >  3) w->turn_q =  3;
    if (w->turn_q < -3) w->turn_q = -3;
}

float walker_bob(const struct walker *w)
{
    return BOB_AMP * w->bob_amt * sinf(w->bob_phase);
}