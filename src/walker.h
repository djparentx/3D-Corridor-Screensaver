#ifndef WALKER_H
#define WALKER_H

#include <stdint.h>

#define WALK_SPEED   1.2f
#define PATH_MIN     10
#define PATH_MAX     30
#define PATH_PTS     (PATH_MAX + 3)

struct walker {
    double   x, z;
    float    yaw;
    /* pts[0] = incoming phantom, pts[1..n] = cell centres, pts[n+1] = outgoing phantom */
    double   pts[PATH_PTS][2];
    int      n;
    int      seg;
    float    u;
    float    bob_phase;
    uint32_t rng;
};

void  walker_init(struct walker *w, double x, double z, uint32_t seed);
void  walker_update(struct walker *w, float dt);
float walker_bob(const struct walker *w);

#endif
