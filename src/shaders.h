#ifndef SHADERS_H
#define SHADERS_H

static const char plane_vs[] =
    "attribute vec2 a_pos;\n"
    "uniform mat4 u_vp;\n"
    "uniform vec2 u_center;\n"
    "uniform float u_size;\n"
    "uniform float u_y;\n"
    "varying vec3 v_world;\n"
    "void main() {\n"
    "  vec3 w = vec3(u_center.x + a_pos.x * u_size, u_y,\n"
    "                u_center.y + a_pos.y * u_size);\n"
    "  v_world = w;\n"
    "  gl_Position = u_vp * vec4(w, 1.0);\n"
    "}\n";

static const char plane_fs[] =
    "precision highp float;\n"
    "varying vec3 v_world;\n"
    "uniform vec3 u_cam;\n"
    "uniform vec3 u_tint;\n"
    "uniform vec3 u_fog;\n"
    "void main() {\n"
    "  vec2 cell = floor(v_world.xz);\n"
    "  float k = mod(cell.x + cell.y, 2.0);\n"
    "  vec3 col = u_tint * (0.85 + 0.15 * k);\n"
    "  float d = distance(v_world, u_cam);\n"
    "  float f = 1.0 - exp(-d * 0.06);\n"
    "  gl_FragColor = vec4(mix(col, u_fog, f), 1.0);\n"
    "}\n";

#endif
