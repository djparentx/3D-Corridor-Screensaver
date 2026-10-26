#ifndef WALKER_H
#define WALKER_H

#include <stdint.h>

#define WALK_SPEED   1.2f
#define PATH_MIN     10
#define PATH_MAX     30
#define PATH_PTS     (PATH_MAX + 3)

enum walker_state {
    WALK_PATH,
    WALK_STOPPING,
    WALK_TURNING,
    WALK_STARING
};

struct walker {
    double   x, z;
    float    yaw;

    /* pts[0] = incoming phantom, pts[1..n] = cell centres, pts[n+1] = outgoing phantom */
    double   pts[PATH_PTS][2];
    int      n;
    int      seg;
    float    u;
    double   vx, vz;            /* velocity on path, m/s */

    /* turn handling */
    int      state;
    int      turn_q;            /* pending turns: <0 left, >0 right */
    int      facing;            /* DIR_N..DIR_W at the stop cell */
    double   h0[2], hm[2], h1[2];
    float    ht, hdur;          /* stop curve progress / duration */
    float    yaw_from, yaw_to, tt;
    float    stare;

    float    bob_phase;
    float    bob_amt;
    uint32_t rng;
};

void  walker_init(struct walker *w, double x, double z, uint32_t seed);
void  walker_update(struct walker *w, float dt);
void  walker_turn(struct walker *w, int dir);   /* -1 left, +1 right */
float walker_bob(const struct walker *w);

#endif