#include <SDL2/SDL.h>
#include <stdio.h>
#include "gl.h"

struct gl_api gl;

int gl_load(void)
{
    int ok = 1;

#define X(type, name)                                              \
    gl.name = (type)SDL_GL_GetProcAddress("gl" #name);             \
    if (!gl.name) {                                                \
        printf("%-14s gl%s\n", "GL MISSING", #name);               \
        ok = 0;                                                    \
    }
    GL_FUNCS(X)
#undef X

    return ok;
}
