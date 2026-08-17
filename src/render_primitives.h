#pragma once

#include "textures.h"    // Tile, UvRect, atlas_uv - used by the *_tex primitive signatures

// ============= Render Primitives (2D quads/bars/circles + 3D cubes/walls/sphere) =============
// Extracted verbatim from main.cpp (original lines ~697-1033, plus the wall-texture cluster
// at ~2065-2187 that sat right after the sky functions): the basic immediate-mode drawing
// helpers used by HUD, menus, minimap and the 3D world renderer. render_quad() had already
// lost its "static" in an earlier stage (minimap.cpp needs it); the rest lose it now for the
// same reason - main.cpp's render_world()/update_game()/HUD code (out of scope for this
// stage) and render_cube_3d_tex() (stays in main.cpp, not part of this stage's list, but
// calls render_cube_outline_3d()) all call these from another translation unit now.
//
// render_player.cpp (a sibling module extracted in the same stage) includes this header
// too: render_player_topdown() draws itself out of render_circle()/render_ellipse()/
// render_quad().
void render_quad(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f);
void render_quad_tex(float x, float y, float w, float h, Tile tile, float tint_r, float tint_g, float tint_b, float a = 1.0f);
void render_bar(float x, float y, float w, float h, float pct, float r, float g, float b);
void render_circle(float cx, float cy, float radius, float r, float g, float b, float a, int segments = 16);
void render_ellipse(float cx, float cy, float rx, float ry, float r, float g, float b, float a, int segments = 16);
void render_rounded_rect(float x, float y, float w, float h, float radius, float r, float g, float b, float a);

void render_cube_outline_3d(float x, float y, float z, float size, float line_width = 1.5f);
void render_cube_3d(float x, float y, float z, float size, float r, float g, float b, float a = 1.0f, bool outline = false);
// render_sphere_3d removed (raylib migration): confirmed dead code, zero call sites anywhere
// in src/ - see the migration plan for details.

// ============= Per-frame fog parameters (raylib migration) =============
// Legacy OpenGL fixed-function fog (glFogf/glFogi/glFogfv, enabled for the whole terrain/
// object/player/beacon render pass in main.cpp's render_world()) has no rlgl/raylib
// equivalent. render_world() computes the fog color/start/end once per frame (same values the
// old glFogfv/glFogf calls used) and stores them here; render_cube_3d()/render_wall_3d_tex()
// (this file) and the local render_plane_3d()/render_plane_3d_tex()/render_cube_3d_tex()
// functions in main.cpp all read this to manually lerp their face colors toward the fog color
// based on distance from the camera, reproducing the old per-pixel GL_LINEAR fog effect (as a
// per-quad/per-face approximation using each shape's center position instead of true
// per-vertex distance - see the migration report for why this is an acceptable simplification).
// render_world() resets fog.enabled=false once the fogged region of the frame ends (mirrors
// the original glDisable(GL_FOG) inside ui_hud.cpp's render_hud()/sky.cpp's render_alien_sky()).
struct FrameFogParams {
    bool enabled = false;
    float start = 0.0f;
    float end = 1000.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f;
};
extern FrameFogParams g_frame_fog;

// Parede vertical texturizada (para diferenca de altura entre tiles vizinhos). Antes eram 4
// funcoes quase identicas (render_wall_3d_tex_xpos/xneg/zpos/zneg), uma por face/eixo
// extrudado - colapsadas aqui numa unica funcao parametrizada por face (Fase 1b do plano de
// refatoracao: unica mudanca de call-site deste estagio). A ordem de vertices/winding de cada
// caso do switch (em render_primitives.cpp) e uma copia exata da funcao original
// correspondente - so a selecao de eixo/sinal virou um parametro.
enum class WallFace { XPos, XNeg, ZPos, ZNeg };
void render_wall_3d_tex(WallFace face, float x, float z, float y0, float y1, Tile tile,
                         float tint_r, float tint_g, float tint_b, float a, float shade);
