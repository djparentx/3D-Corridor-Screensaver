#include <SDL2/SDL.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "gl.h"
#include "mat4.h"
#include "shaders.h"
#include "world.h"
#include "mesh.h"

#define EYE_HEIGHT     1.6f
#define WALK_SPEED     1.2f
#define PLANE_SIZE     80.0f
#define FOV_DEG        70.0f
#define Z_NEAR         0.1f
#define Z_FAR          80.0f
#define INPUT_GRACE    500

#define FOG_R          0.42f
#define FOG_G          0.40f
#define FOG_B          0.28f

#define VERTEX_STRIDE  (VERTEX_FLOATS * (GLsizei)sizeof(float))

struct plane_prog {
    GLuint id;
    GLint  vp, size, y, frac, eye, tint, fog;
};

struct wall_prog {
    GLuint id;
    GLint  vp, offset, eye, tint, fog;
};

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = gl.CreateShader(type);
    GLint ok;
    char log[512];

    gl.ShaderSource(s, 1, &src, NULL);
    gl.CompileShader(s);
    gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        gl.GetShaderInfoLog(s, sizeof(log), NULL, log);
        printf("%-14s %s\n", "SHADER ERROR", log);
    }
    return s;
}

static GLuint build_program(const char *vs, const char *fs)
{
    GLuint p = gl.CreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    GLint ok;
    char log[512];

    gl.AttachShader(p, v);
    gl.AttachShader(p, f);
    gl.BindAttribLocation(p, 0, "a_pos");
    gl.BindAttribLocation(p, 1, "a_uv");
    gl.BindAttribLocation(p, 2, "a_nrm");
    gl.LinkProgram(p);
    gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        gl.GetProgramInfoLog(p, sizeof(log), NULL, log);
        printf("%-14s %s\n", "LINK ERROR", log);
    }
    gl.DeleteShader(v);
    gl.DeleteShader(f);
    return p;
}

static int is_exit_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT:
    case SDL_KEYDOWN:
    case SDL_JOYBUTTONDOWN:
        return 1;
    case SDL_JOYHATMOTION:
        return e->jhat.value != SDL_HAT_CENTERED;
    }
    return 0;
}

int main(void)
{
    static const float quad[] = { -1, -1,  1, -1,  -1, 1,  1, 1 };
    SDL_DisplayMode dm;
    SDL_Window *win;
    SDL_GLContext ctx;
    struct plane_prog pp;
    struct wall_prog wp;
    struct chunk *chunks;
    GLuint quad_vbo;
    Uint64 freq, t_prev, t_now;
    Uint32 t_start, seed;
    float proj[16], rot[16], trans[16], view[16], vp[16];
    double cam_x = 1.0, cam_z = 1.0;
    float yaw = 0.0f, dt;
    int w, h, i, frames = 0;

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        printf("%-14s %s\n", "SDL_Init FAIL", SDL_GetError());
        return 1;
    }

    for (i = 0; i < SDL_NumJoysticks(); i++)
        SDL_JoystickOpen(i);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    if (SDL_GL_LoadLibrary(NULL) != 0 &&
        SDL_GL_LoadLibrary("libGLESv2.so") != 0) {
        printf("%-14s %s\n", "GL LIB FAIL", SDL_GetError());
        return 1;
    }

    SDL_GetCurrentDisplayMode(0, &dm);

    win = SDL_CreateWindow("3D_Backrooms", 0, 0, dm.w, dm.h,
                           SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!win) {
        printf("%-14s %s\n", "WINDOW FAIL", SDL_GetError());
        return 1;
    }

    ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        printf("%-14s %s\n", "CONTEXT FAIL", SDL_GetError());
        return 1;
    }
    SDL_GL_SetSwapInterval(1);
    SDL_ShowCursor(SDL_DISABLE);

    if (!gl_load())
        return 1;

    SDL_GL_GetDrawableSize(win, &w, &h);
    seed = (Uint32)time(NULL);

    printf("%-14s %s\n",          "SDL driver",  SDL_GetCurrentVideoDriver());
    printf("%-14s %dx%d @%dHz\n", "Display",     w, h, dm.refresh_rate);
    printf("%-14s %s\n",          "GL renderer", gl.GetString(GL_RENDERER));
    printf("%-14s %s\n",          "GL version",  gl.GetString(GL_VERSION));
    printf("%-14s %u\n",          "World seed",  seed);

    pp.id   = build_program(plane_vs, plane_fs);
    pp.vp   = gl.GetUniformLocation(pp.id, "u_vp");
    pp.size = gl.GetUniformLocation(pp.id, "u_size");
    pp.y    = gl.GetUniformLocation(pp.id, "u_y");
    pp.frac = gl.GetUniformLocation(pp.id, "u_frac");
    pp.eye  = gl.GetUniformLocation(pp.id, "u_eye");
    pp.tint = gl.GetUniformLocation(pp.id, "u_tint");
    pp.fog  = gl.GetUniformLocation(pp.id, "u_fog");

    wp.id     = build_program(wall_vs, wall_fs);
    wp.vp     = gl.GetUniformLocation(wp.id, "u_vp");
    wp.offset = gl.GetUniformLocation(wp.id, "u_offset");
    wp.eye    = gl.GetUniformLocation(wp.id, "u_eye");
    wp.tint   = gl.GetUniformLocation(wp.id, "u_tint");
    wp.fog    = gl.GetUniformLocation(wp.id, "u_fog");

    gl.GenBuffers(1, &quad_vbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    gl.EnableVertexAttribArray(0);

    gl.Viewport(0, 0, w, h);
    gl.Enable(GL_DEPTH_TEST);
    gl.ClearColor(FOG_R, FOG_G, FOG_B, 1.0f);

    gl.UseProgram(pp.id);
    gl.Uniform3f(pp.fog, FOG_R, FOG_G, FOG_B);
    gl.Uniform3f(pp.eye, 0.0f, EYE_HEIGHT, 0.0f);
    gl.Uniform1f(pp.size, PLANE_SIZE);

    gl.UseProgram(wp.id);
    gl.Uniform3f(wp.fog, FOG_R, FOG_G, FOG_B);
    gl.Uniform3f(wp.eye, 0.0f, EYE_HEIGHT, 0.0f);
    gl.Uniform3f(wp.tint, 0.80f, 0.76f, 0.52f);

    mat4_perspective(proj, FOV_DEG * (float)M_PI / 180.0f,
                     (float)w / (float)h, Z_NEAR, Z_FAR);
    mat4_translate(trans, 0.0f, -EYE_HEIGHT, 0.0f);

    world_init(seed);

    freq    = SDL_GetPerformanceFrequency();
    t_prev  = SDL_GetPerformanceCounter();
    t_start = SDL_GetTicks();

    while (g_running) {
        SDL_Event e;

        while (SDL_PollEvent(&e)) {
            if (is_exit_event(&e) && SDL_GetTicks() - t_start > INPUT_GRACE)
                g_running = 0;
        }

        t_now  = SDL_GetPerformanceCounter();
        dt     = (float)(t_now - t_prev) / (float)freq;
        t_prev = t_now;

        cam_x -= sin(yaw) * WALK_SPEED * dt;
        cam_z -= cos(yaw) * WALK_SPEED * dt;

        world_update(cam_x, cam_z);

        mat4_rotate_y(rot, -yaw);
        mat4_mul(view, rot, trans);
        mat4_mul(vp, proj, view);

        gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* floor + ceiling */
        gl.UseProgram(pp.id);
        gl.UniformMatrix4fv(pp.vp, 1, GL_FALSE, vp);
        gl.Uniform2f(pp.frac, (float)fmod(cam_x, 2.0), (float)fmod(cam_z, 2.0));
        gl.BindBuffer(GL_ARRAY_BUFFER, quad_vbo);
        gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void *)0);

        gl.Uniform1f(pp.y, 0.0f);
        gl.Uniform3f(pp.tint, 0.55f, 0.47f, 0.33f);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        gl.Uniform1f(pp.y, CEIL_HEIGHT);
        gl.Uniform3f(pp.tint, 0.85f, 0.84f, 0.72f);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        /* walls */
        gl.UseProgram(wp.id);
        gl.UniformMatrix4fv(wp.vp, 1, GL_FALSE, vp);
        gl.EnableVertexAttribArray(1);
        gl.EnableVertexAttribArray(2);

        chunks = world_chunks();
        for (i = 0; i < CHUNK_SLOTS; i++) {
            struct chunk *c = &chunks[i];

            if (!c->valid || !c->index_count)
                continue;

            gl.Uniform3f(wp.offset,
                         (float)(c->cx * (double)CHUNK_SIZE - cam_x), 0.0f,
                         (float)(c->cz * (double)CHUNK_SIZE - cam_z));

            gl.BindBuffer(GL_ARRAY_BUFFER, c->vbo);
            gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, c->ibo);
            gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, VERTEX_STRIDE,
                                   (const void *)(uintptr_t)0);
            gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, VERTEX_STRIDE,
                                   (const void *)(uintptr_t)(3 * sizeof(float)));
            gl.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, VERTEX_STRIDE,
                                   (const void *)(uintptr_t)(5 * sizeof(float)));
            gl.DrawElements(GL_TRIANGLES, c->index_count, GL_UNSIGNED_SHORT,
                            (const void *)0);
        }

        gl.DisableVertexAttribArray(1);
        gl.DisableVertexAttribArray(2);

        SDL_GL_SwapWindow(win);
        frames++;
    }

    printf("%-14s %.1f\n", "Average FPS",
           frames * 1000.0f / (float)(SDL_GetTicks() - t_start));

    world_free();
    gl.DeleteBuffers(1, &quad_vbo);
    gl.DeleteProgram(pp.id);
    gl.DeleteProgram(wp.id);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}