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
    "uniform sampler2D u_tex;\n"
    "uniform float u_scale;\n"
    "uniform vec2 u_frac;\n"
    "uniform vec3 u_eye;\n"
    "uniform vec3 u_fog;\n"
    "void main() {\n"
    "  vec2 uv = (v_rel.xz + u_frac) * u_scale;\n"
    "  vec3 col = texture2D(u_tex, uv).rgb;\n"
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
    "uniform vec2 u_uvscale;\n"
    "varying vec3 v_rel;\n"
    "varying vec2 v_uv;\n"
    "varying float v_shade;\n"
    "void main() {\n"
    "  vec3 r = a_pos + u_offset;\n"
    "  v_rel = r;\n"
    "  v_uv = a_uv * u_uvscale;\n"
    "  v_shade = 0.82 + 0.18 * abs(a_nrm.x);\n"
    "  gl_Position = u_vp * vec4(r, 1.0);\n"
    "}\n";

static const char wall_fs[] =
    "precision highp float;\n"
    "varying vec3 v_rel;\n"
    "varying vec2 v_uv;\n"
    "varying float v_shade;\n"
    "uniform sampler2D u_tex;\n"
    "uniform vec3 u_eye;\n"
    "uniform vec3 u_fog;\n"
    "void main() {\n"
    "  vec3 col = texture2D(u_tex, v_uv).rgb * v_shade;\n"
    "  float d = distance(v_rel, u_eye);\n"
    "  float f = 1.0 - exp(-d * 0.06);\n"
    "  gl_FragColor = vec4(mix(col, u_fog, f), 1.0);\n"
    "}\n";

#endif