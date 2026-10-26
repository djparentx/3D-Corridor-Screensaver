#ifndef MAT4_H
#define MAT4_H

#include <math.h>
#include <string.h>

/* column-major: m[col * 4 + row] */

static inline void mat4_identity(float *m)
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static inline void mat4_mul(float *r, const float *a, const float *b)
{
    float t[16];
    int c, row;

    for (c = 0; c < 4; c++)
        for (row = 0; row < 4; row++)
            t[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] +
                             a[1 * 4 + row] * b[c * 4 + 1] +
                             a[2 * 4 + row] * b[c * 4 + 2] +
                             a[3 * 4 + row] * b[c * 4 + 3];
    memcpy(r, t, sizeof(t));
}

static inline void mat4_perspective(float *m, float fovy, float aspect,
                                    float zn, float zf)
{
    float f = 1.0f / tanf(fovy * 0.5f);

    memset(m, 0, 16 * sizeof(float));
    m[0]  = f / aspect;
    m[5]  = f;
    m[10] = (zf + zn) / (zn - zf);
    m[11] = -1.0f;
    m[14] = 2.0f * zf * zn / (zn - zf);
}

static inline void mat4_translate(float *m, float x, float y, float z)
{
    mat4_identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
}

static inline void mat4_rotate_y(float *m, float a)
{
    float c = cosf(a), s = sinf(a);

    mat4_identity(m);
    m[0]  = c;
    m[2]  = -s;
    m[8]  = s;
    m[10] = c;
}

#endif
