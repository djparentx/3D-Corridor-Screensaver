#include <SDL2/SDL.h>
#include <signal.h>
#include <stdio.h>
#include <math.h>
#include "gl.h"
#include "mat4.h"
#include "shaders.h"

#define EYE_HEIGHT   1.6f
#define CEIL_HEIGHT  2.7f
#define WALK_SPEED   1.2f
#define PLANE_SIZE   80.0f
#define FOV_DEG      70.0f
#define Z_NEAR       0.1f
#define Z_FAR        80.0f
#define INPUT_GRACE  500

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
    GLuint prog, vbo;
    GLint u_vp, u_center, u_size, u_y, u_cam, u_tint, u_fog;
    Uint64 freq, t_prev, t_now;
    Uint32 t_start;
    float proj[16], rot[16], trans[16], view[16], vp[16];
    float cam_x = 0.0f, cam_z = 0.0f, yaw = 0.0f, dt;
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

    printf("%-14s %s\n",          "SDL driver",  SDL_GetCurrentVideoDriver());
    printf("%-14s %dx%d @%dHz\n", "Display",     w, h, dm.refresh_rate);
    printf("%-14s %s\n",          "GL renderer", gl.GetString(GL_RENDERER));
    printf("%-14s %s\n",          "GL version",  gl.GetString(GL_VERSION));

    prog     = build_program(plane_vs, plane_fs);
    u_vp     = gl.GetUniformLocation(prog, "u_vp");
    u_center = gl.GetUniformLocation(prog, "u_center");
    u_size   = gl.GetUniformLocation(prog, "u_size");
    u_y      = gl.GetUniformLocation(prog, "u_y");
    u_cam    = gl.GetUniformLocation(prog, "u_cam");
    u_tint   = gl.GetUniformLocation(prog, "u_tint");
    u_fog    = gl.GetUniformLocation(prog, "u_fog");

    gl.GenBuffers(1, &vbo);
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    gl.EnableVertexAttribArray(0);

    gl.Viewport(0, 0, w, h);
    gl.Enable(GL_DEPTH_TEST);
    gl.ClearColor(0.42f, 0.40f, 0.28f, 1.0f);

    gl.UseProgram(prog);
    gl.Uniform3f(u_fog, 0.42f, 0.40f, 0.28f);
    gl.Uniform1f(u_size, PLANE_SIZE);

    mat4_perspective(proj, FOV_DEG * (float)M_PI / 180.0f,
                     (float)w / (float)h, Z_NEAR, Z_FAR);

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

        cam_x -= sinf(yaw) * WALK_SPEED * dt;
        cam_z -= cosf(yaw) * WALK_SPEED * dt;

        mat4_rotate_y(rot, -yaw);
        mat4_translate(trans, -cam_x, -EYE_HEIGHT, -cam_z);
        mat4_mul(view, rot, trans);
        mat4_mul(vp, proj, view);

        gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        gl.UniformMatrix4fv(u_vp, 1, GL_FALSE, vp);
        gl.Uniform2f(u_center, cam_x, cam_z);
        gl.Uniform3f(u_cam, cam_x, EYE_HEIGHT, cam_z);

        gl.Uniform1f(u_y, 0.0f);
        gl.Uniform3f(u_tint, 0.55f, 0.47f, 0.33f);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        gl.Uniform1f(u_y, CEIL_HEIGHT);
        gl.Uniform3f(u_tint, 0.85f, 0.84f, 0.72f);
        gl.DrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        SDL_GL_SwapWindow(win);
        frames++;
    }

    printf("%-14s %.1f\n", "Average FPS",
           frames * 1000.0f / (float)(SDL_GetTicks() - t_start));

    gl.DeleteBuffers(1, &vbo);
    gl.DeleteProgram(prog);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
