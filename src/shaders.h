#ifndef SHADERS_H
#define SHADERS_H

/* floor / ceiling: camera-relative plane */
static const char plane_vs[] =
    "attribute vec2 a_pos;\n"
    "uniform mat4 u_vp;\n"
    "uniform float u_size;\n"
    "uniform float u_y;\n"
    "varying vec3 v_rel;\n"
    "void main() {\n"
    "  vec3 r = vec3(a_pos.x * u_size, u_y, a_pos.y * u_size);\n"
    "  v_rel = r;\n"
    "  gl_Position = u_vp * vec4(r, 1.0);\n"
    "}\n";

static const char plane_fs[] =
    "precision highp float;\n"
    "varying vec3 v_rel;\n"
    "uniform vec2 u_frac;\n"
    "uniform vec3 u_eye;\n"
    "uniform vec3 u_tint;\n"
    "uniform vec3 u_fog;\n"
    "void main() {\n"
    "  vec2 cell = floor(v_rel.xz + u_frac);\n"
    "  float k = mod(cell.x + cell.y, 2.0);\n"
    "  vec3 col = u_tint * (0.85 + 0.15 * k);\n"
    "  float d = distance(v_rel, u_eye);\n"
    "  float f = 1.0 - exp(-d * 0.06);\n"
    "  gl_FragColor = vec4(mix(col, u_fog, f), 1.0);\n"
    "}\n";

/* walls: chunk-local vertices + chunk offset relative to camera */
static const char wall_vs[] =
    "attribute vec3 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "attribute vec2 a_nrm;\n"
    "uniform mat4 u_vp;\n"
    "uniform vec3 u_offset;\n"
    "varying vec3 v_rel;\n"
    "varying vec2 v_uv;\n"
    "varying float v_shade;\n"
    "void main() {\n"
    "  vec3 r = a_pos + u_offset;\n"
    "  v_rel = r;\n"
    "  v_uv = a_uv;\n"
    "  v_shade = 0.78 + 0.22 * abs(a_nrm.x);\n"
    "  gl_Position = u_vp * vec4(r, 1.0);\n"
    "}\n";

static const char wall_fs[] =
    "precision highp float;\n"
    "varying vec3 v_rel;\n"
    "varying vec2 v_uv;\n"
    "varying float v_shade;\n"
    "uniform vec3 u_eye;\n"
    "uniform vec3 u_tint;\n"
    "uniform vec3 u_fog;\n"
    "void main() {\n"
    "  vec3 col = u_tint * v_shade;\n"
    "  if (v_rel.y < 0.12) col *= 0.7;\n"
    "  float d = distance(v_rel, u_eye);\n"
    "  float f = 1.0 - exp(-d * 0.06);\n"
    "  gl_FragColor = vec4(mix(col, u_fog, f), 1.0);\n"
    "}\n";

#endif