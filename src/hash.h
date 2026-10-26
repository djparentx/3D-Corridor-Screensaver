#ifndef HASH_H
#define HASH_H

#include <stdint.h>

static inline uint32_t hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static inline uint32_t hash_2i(int32_t x, int32_t y, uint32_t salt)
{
    return hash_u32((uint32_t)x * 73856093u ^ hash_u32((uint32_t)y * 19349663u ^ salt));
}

#endif
