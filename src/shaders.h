#ifndef SHADERS_H
#define SHADERS_H

/* Shared lighting. Literals must match:
   2.7  = CEIL_HEIGHT     4.0 = PANEL_SPACING
   1.0  = PANEL_OFFSET    32.0 = LIGHT_TEX_SIZE */
#define LIGHT_GLSL \
    "uniform sampler2D u_light;\n" \
    "uniform vec2 u_frac;\n" \
    "uniform vec2 u_lorigin;\n" \
    "float level_at(vec2 g) {\n" \
    "  return texture2D(u_light, (g - u_lorigin + 0.5) / 32.0).r;\n" \
    "}\n" \
    "vec3 light_at(vec3 rel, vec2 pw) {\n" \
    "  vec2 g = (pw - 1.0) * 0.25;\n" \
    "  vec2 n = floor(g + 0.5);\n" \
    "  vec2 dp = pw - (n * 4.0 + 1.0);\n" \
    "  float dy = 2.7 - rel.y;\n" \
    "  float glow = level_at(n) * exp(-(dot(dp, dp) + dy * dy) * 0.7);\n" \
    "  return vec3(1.0, 0.97, 0.86) * (0.30 + 0.70 * level_at(g) + 0.45 * glow);\n" \
    "}\n"

#define HASH_GLSL \
    "float hash12(vec2 p) {\n" \
    "  vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n" \
    "  p3 += dot(p3, p3.yzx + 33.33);\n" \
    "  return fract((p3.x + p3.y) * p3.z);\n" \
    "}\n"

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
    "uniform float u_ceil;\n"
    "uniform vec2 u_base;\n"
    "uniform vec3 u_eye;\n"
    "uniform vec3 u_fog;\n"
    "uniform float u_fogk;\n"
    LIGHT_GLSL
    HASH_GLSL
    "void main() {\n"
    "  vec2 pw = v_rel.xz + u_frac;\n"
    "  vec3 col;\n"
    "  if (u_ceil > 0.5) {\n"
    "    vec2 uv = (pw + vec2(0.0, 0.25)) * u_scale;\n"
    "    col = texture2D(u_tex, uv).rgb * light_at(v_rel, pw);\n"
    "    vec2 n = floor((pw - 1.0) * 0.25 + 0.5);\n"
    "    vec2 dp = abs(pw - (n * 4.0 + 1.0));\n"
    "    if (dp.x < 0.5 && dp.y < 0.25) {\n"
    "      if (dp.x < 0.47 && dp.y < 0.22)\n"
    "        col = vec3(1.0, 0.98, 0.88) * (0.22 + 0.85 * level_at(n));\n"
    "      else\n"
    "        col = vec3(0.74, 0.73, 0.67) * light_at(v_rel, pw);\n"
    "    }\n"
    "  } else {\n"
    "    vec2 uv = pw * u_scale;\n"
    "    vec2 tile = floor(uv);\n"
    "    vec2 f = uv - tile;\n"
    "    float r = floor(hash12(tile + u_base * 16.0) * 4.0);\n"
    "    if (r == 1.0) f = vec2(f.y, 1.0 - f.x);\n"
    "    else if (r == 2.0) f = 1.0 - f;\n"
    "    else if (r == 3.0) f = vec2(1.0 - f.y, f.x);\n"
    "    col = texture2D(u_tex, f).rgb * light_at(v_rel, pw);\n"
    "  }\n"
    "  float d = distance(v_rel, u_eye);\n"
    "  float k = 1.0 - exp(-d * u_fogk);\n"
    "  gl_FragColor = vec4(mix(col, u_fog, k), 1.0);\n"
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
    "  v_shade = 0.85 + 0.15 * abs(a_nrm.x);\n"
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
    "uniform float u_fogk;\n"
    LIGHT_GLSL
    "void main() {\n"
    "  vec2 pw = v_rel.xz + u_frac;\n"
    "  vec3 col = texture2D(u_tex, v_uv).rgb * v_shade * light_at(v_rel, pw);\n"
    "  float d = distance(v_rel, u_eye);\n"
    "  float k = 1.0 - exp(-d * u_fogk);\n"
    "  gl_FragColor = vec4(mix(col, u_fog, k), 1.0);\n"
    "}\n";

#endif