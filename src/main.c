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
#include "tex.h"
#include "light.h"
#include "walker.h"
#include "settings.h"
#include "audio.h"

#define EYE_HEIGHT     1.6f
#define PLANE_SIZE     80.0f
#define FOV_DEG        70.0f
#define Z_NEAR         0.1f
#define Z_FAR          80.0f
#define INPUT_GRACE    500

#define FOG_R          0.34f
#define FOG_G          0.32f
#define FOG_B          0.22f
#define FOG_K          0.055f

#define VERTEX_STRIDE  (VERTEX_FLOATS * (GLsizei)sizeof(float))

struct plane_prog {
    GLuint id;
    GLint  vp, size, y, ceil, scale, base, frac, lorigin, light, tex, eye, fog, fogk;
};

struct wall_prog {
    GLuint id;
    GLint  vp, offset, uvscale, frac, lorigin, light, tex, eye, fog, fogk;
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

int main(int argc, char **argv)
{
    static const float quad[] = { -1, -1,  1, -1,  -1, 1,  1, 1 };
    SDL_DisplayMode dm;
    SDL_Window *win;
    SDL_GLContext ctx;
    struct plane_prog pp;
    struct wall_prog wp;
    struct textures tex;
    struct chunk *chunks;
    GLuint quad_vbo;
    Uint64 freq, t_prev, t_now;
    Uint32 t_start, seed;
    float proj[16], rot[16], trans[16], view[16], vp[16];
    double cam_x = 1.0, cam_z = 1.0;
    int32_t ccx, ccz;
    struct walker wk;
    struct settings cfg;
    float amb;
    float dt, now, frac_x, frac_z, lo_x, lo_z;
    int w, h, i, v, frames = 0;

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    settings_parse(&cfg, argc, argv);
    settings_print(&cfg);
    amb = 0.1f + 0.9f * (float)cfg.ambient / 100.0f;

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

    pp.id      = build_program(plane_vs, plane_fs);
    pp.vp      = gl.GetUniformLocation(pp.id, "u_vp");
    pp.size    = gl.GetUniformLocation(pp.id, "u_size");
    pp.y       = gl.GetUniformLocation(pp.id, "u_y");
    pp.ceil    = gl.GetUniformLocation(pp.id, "u_ceil");
    pp.scale   = gl.GetUniformLocation(pp.id, "u_scale");
    pp.base    = gl.GetUniformLocation(pp.id, "u_base");
    pp.frac    = gl.GetUniformLocation(pp.id, "u_frac");
    pp.lorigin = gl.GetUniformLocation(pp.id, "u_lorigin");
    pp.light   = gl.GetUniformLocation(pp.id, "u_light");
    pp.tex     = gl.GetUniformLocation(pp.id, "u_tex");
    pp.eye     = gl.GetUniformLocation(pp.id, "u_eye");
    pp.fog     = gl.GetUniformLocation(pp.id, "u_fog");
    pp.fogk    = gl.GetUniformLocation(pp.id, "u_fogk");

    wp.id      = build_program(wall_vs, wall_fs);
    wp.vp      = gl.GetUniformLocation(wp.id, "u_vp");
    wp.offset  = gl.GetUniformLocation(wp.id, "u_offset");
    wp.uvscale = gl.GetUniformLocation(wp.id, "u_uvscale");
    wp.frac    = gl.GetUniformLocation(wp.id, "u_frac");
    wp.lorigin = gl.GetUniformLocation(wp.id, "u_lorigin");
    wp.light   = gl.GetUniformLocation(wp.id, "u_light");
    wp.tex     = gl.GetUniformLocation(wp.id, "u_tex");
    wp.eye     = gl.GetUniformLocation(wp.id, "u_eye");
    wp.fog     = gl.GetUniformLocation(wp.id, "u_fog");
    wp.fogk    = gl.GetUniformLocation(wp.id, "u_fogk");

    gl.GenBuffers(1, &quad_vbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    gl.EnableVertexAttribArray(0);

    gl.Viewport(0, 0, w, h);
    gl.Enable(GL_DEPTH_TEST);
    gl.ClearColor(FOG_R, FOG_G, FOG_B, 1.0f);

    /* unit 1 = light panel states, unit 0 = material */
    gl.ActiveTexture(GL_TEXTURE1);
    light_init(cfg.panels, cfg.flicker);
    gl.ActiveTexture(GL_TEXTURE0);
    tex_create(&tex);

    gl.UseProgram(pp.id);
    gl.Uniform1f(pp.size, PLANE_SIZE);
    gl.Uniform1f(pp.scale, 1.0f / FLOOR_TEX_SIZE);
    gl.Uniform1i(pp.tex, 0);
    gl.Uniform1i(pp.light, 1);
    gl.Uniform3f(pp.eye, 0.0f, EYE_HEIGHT, 0.0f);
    gl.Uniform3f(pp.fog, FOG_R, FOG_G, FOG_B);
    gl.Uniform1f(pp.fogk, FOG_K * (float)cfg.fog);
    gl.Uniform1f(gl.GetUniformLocation(pp.id, "u_ambient"), amb);

    gl.UseProgram(wp.id);
    gl.Uniform2f(wp.uvscale, 1.0f / WALL_TEX_WIDTH, 1.0f / CEIL_HEIGHT);
    gl.Uniform1i(wp.tex, 0);
    gl.Uniform1i(wp.light, 1);
    gl.Uniform3f(wp.eye, 0.0f, EYE_HEIGHT, 0.0f);
    gl.Uniform3f(wp.fog, FOG_R, FOG_G, FOG_B);
    gl.Uniform1f(wp.fogk, FOG_K * (float)cfg.fog);
    gl.Uniform1f(gl.GetUniformLocation(wp.id, "u_ambient"), amb);

    mat4_perspective(proj, FOV_DEG * (float)M_PI / 180.0f,
                     (float)w / (float)h, Z_NEAR, Z_FAR);

    world_init(seed);
    world_update(cam_x, cam_z);
    walker_init(&wk, cam_x, cam_z, seed);
    audio_start(cfg.volume);

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
        now    = (float)(SDL_GetTicks() - t_start) / 1000.0f;

        walker_update(&wk, dt * cfg.speed);
        cam_x = wk.x;
        cam_z = wk.z;

        world_update(cam_x, cam_z);

        ccx    = (int32_t)floor(cam_x / CHUNK_SIZE);
        ccz    = (int32_t)floor(cam_z / CHUNK_SIZE);
        frac_x = (float)(cam_x - ccx * (double)CHUNK_SIZE);
        frac_z = (float)(cam_z - ccz * (double)CHUNK_SIZE);

        gl.ActiveTexture(GL_TEXTURE1);
        light_update(cam_x, cam_z, now, &lo_x, &lo_z);
        gl.ActiveTexture(GL_TEXTURE0);

        mat4_translate(trans, 0.0f, -(EYE_HEIGHT + walker_bob(&wk)), 0.0f);
        mat4_rotate_y(rot, -wk.yaw);
        mat4_mul(view, rot, trans);
        mat4_mul(vp, proj, view);

        gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        /* floor + ceiling */
        gl.UseProgram(pp.id);
        gl.UniformMatrix4fv(pp.vp, 1, GL_FALSE, vp);
        gl.Uniform2f(pp.frac, frac_x, frac_z);
        gl.Uniform2f(pp.lorigin, lo_x, lo_z);
        gl.Uniform2f(pp.base, (float)ccx, (float)ccz);
        gl.BindBuffer(GL_ARRAY_BUFFER, quad_vbo);
        gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (const void *)0);

        gl.Uniform1f(pp.y, 0.0f);
        gl.Uniform1f(pp.ceil, 0.0f);
        gl.BindTexture(GL_TEXTURE_2D, tex.carpet);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        gl.Uniform1f(pp.y, CEIL_HEIGHT);
        gl.Uniform1f(pp.ceil, 1.0f);
        gl.BindTexture(GL_TEXTURE_2D, tex.ceiling);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        /* walls */
        gl.UseProgram(wp.id);
        gl.UniformMatrix4fv(wp.vp, 1, GL_FALSE, vp);
        gl.Uniform2f(wp.frac, frac_x, frac_z);
        gl.Uniform2f(wp.lorigin, lo_x, lo_z);
        gl.EnableVertexAttribArray(1);
        gl.EnableVertexAttribArray(2);

        chunks = world_chunks();
        for (i = 0; i < CHUNK_SLOTS; i++) {
            struct chunk *c = &chunks[i];

            if (!c->valid || !c->vbo)
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

            for (v = 0; v < WALL_VARIANTS; v++) {
                if (!c->idx_count[v])
                    continue;
                gl.BindTexture(GL_TEXTURE_2D, tex.wall[v]);
                gl.DrawElements(GL_TRIANGLES, c->idx_count[v], GL_UNSIGNED_SHORT,
                                (const void *)(uintptr_t)(c->idx_start[v] * sizeof(uint16_t)));
            }
        }

        gl.DisableVertexAttribArray(1);
        gl.DisableVertexAttribArray(2);

        SDL_GL_SwapWindow(win);
        frames++;
    }

    printf("%-14s %.1f\n", "Average FPS",
           frames * 1000.0f / (float)(SDL_GetTicks() - t_start));

    world_free();
    audio_stop();
    tex_destroy(&tex);
    light_free();
    gl.DeleteBuffers(1, &quad_vbo);
    gl.DeleteProgram(pp.id);
    gl.DeleteProgram(wp.id);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}