#include "raylib_platform.h"
#include "render_primitives.h"

#include "math_core.h"    // kPi, clamp01
#include "textures.h"     // Tile, UvRect, atlas_uv, kAtlasSizePx
#include "camera.h"       // g_camera.position (for fog distance)
#include "noise.h"        // lerp

#include <cmath>

// ============= Per-frame fog parameters =============
// Definition of the extern declared in render_primitives.h. main.cpp's render_world() sets
// these once per frame before the terrain loop runs (and clears .enabled once the fogged
// region of the frame ends); this file's render_cube_3d()/render_wall_3d_tex() (and main.cpp's
// own local render_plane_3d()/render_plane_3d_tex()/render_cube_3d_tex()) read them.
FrameFogParams g_frame_fog;

// Applies the current frame's fog (if enabled) to an already-shaded color, based on the
// distance from the camera to the given world-space position. Reproduces the old GL_LINEAR
// fixed-function fog formula: factor = clamp01((end - dist) / (end - start)); factor=1 near
// (original color unchanged), factor=0 far (fully replaced by the fog color).
static void apply_frame_fog(float wx, float wy, float wz, float& r, float& g, float& b) {
    if (!g_frame_fog.enabled) return;
    float dx = wx - g_camera.position.x;
    float dy = wy - g_camera.position.y;
    float dz = wz - g_camera.position.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float span = std::max(0.0001f, g_frame_fog.end - g_frame_fog.start);
    float factor = clamp01((g_frame_fog.end - dist) / span);
    r = lerp(g_frame_fog.r, r, factor);
    g = lerp(g_frame_fog.g, g, factor);
    b = lerp(g_frame_fog.b, b, factor);
}

static inline Color color_f(float r, float g, float b, float a) {
    Color c;
    c.r = (unsigned char)std::clamp((int)(r * 255.0f), 0, 255);
    c.g = (unsigned char)std::clamp((int)(g * 255.0f), 0, 255);
    c.b = (unsigned char)std::clamp((int)(b * 255.0f), 0, 255);
    c.a = (unsigned char)std::clamp((int)(a * 255.0f), 0, 255);
    return c;
}

// ============= Rendering Helpers =============
void render_quad(float x, float y, float w, float h, float r, float g, float b, float a) {
    // DrawRectangleRec (not DrawRectangle) - the latter truncates to int pixel coordinates,
    // which would visibly jitter the many sub-pixel float positions used throughout the HUD/
    // menus/progress bars (bounce/hover animations, fine bar-fill widths, etc.).
    DrawRectangleRec({x, y, w, h}, color_f(r, g, b, a));
}

// Quad 2D texturizado (tile do atlas). Migrado para DrawTexturePro: atlas_uv() continua
// devolvendo o retangulo normalizado (0..1) na mesma convencao "origem embaixo" de sempre
// (ver textures.h) - o unico ajuste necessario aqui e converter esse retangulo normalizado
// para um Rectangle em pixels (que DrawTexturePro exige) com altura NEGATIVA, reproduzindo a
// mesma direcao de amostragem v1->v0 que o codigo antigo usava (glTexCoord2f(u0,v1) no vertice
// de tela de cima, glTexCoord2f(u0,v0) no de baixo) sem precisar mudar a convencao de
// coordenadas do atlas em si (ver o comentario de atlas_uv() em textures.h).
void render_quad_tex(float x, float y, float w, float h, Tile tile, float tint_r, float tint_g, float tint_b, float a) {
    UvRect uv = atlas_uv(tile);
    Texture2D atlas_tex{};
    atlas_tex.id = g_tex_atlas;
    atlas_tex.width = kAtlasSizePx;
    atlas_tex.height = kAtlasSizePx;
    atlas_tex.mipmaps = 1;
    atlas_tex.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;

    Rectangle src;
    src.x = uv.u0 * (float)kAtlasSizePx;
    src.width = (uv.u1 - uv.u0) * (float)kAtlasSizePx;
    src.y = uv.v1 * (float)kAtlasSizePx;
    src.height = -(uv.v1 - uv.v0) * (float)kAtlasSizePx;

    Rectangle dst{x, y, w, h};
    DrawTexturePro(atlas_tex, src, dst, {0.0f, 0.0f}, 0.0f, color_f(tint_r, tint_g, tint_b, a));
}

void render_bar(float x, float y, float w, float h, float pct, float r, float g, float b) {
    render_quad(x, y, w, h, 0.0f, 0.0f, 0.0f, 0.55f);
    render_quad(x + 2.0f, y + 2.0f, (w - 4.0f) * clamp01(pct), h - 4.0f, r, g, b, 0.92f);
}

// ============= Astronaut Rendering =============
void render_circle(float cx, float cy, float radius, float r, float g, float b, float a, int segments) {
    // DrawCircleSector: startAngle/endAngle in degrees, segment count matches the old
    // GL_TRIANGLE_FAN's `segments` fan slices exactly (render_rounded_rect calls this with
    // segments=8 for its deliberately low-poly corners - preserved here).
    DrawCircleSector({cx, cy}, radius, 0.0f, 360.0f, segments, color_f(r, g, b, a));
}

void render_ellipse(float cx, float cy, float rx, float ry, float r, float g, float b, float a, int segments) {
    (void)segments; // DrawEllipse has no segment-count parameter; confirmed safe (see plan).
    DrawEllipse((int)cx, (int)cy, rx, ry, color_f(r, g, b, a));
}

void render_rounded_rect(float x, float y, float w, float h, float radius, float r, float g, float b, float a) {
    float roundness = clamp01(radius / (0.5f * std::min(w, h)));
    Rectangle rec{x, y, w, h};
    DrawRectangleRounded(rec, roundness, 8, color_f(r, g, b, a));
}

// ============= Renderizacao 3D (Estilo Minicraft) =============

// Renderizar outline de um cubo (bordas pretas estilo pixel art)
void render_cube_outline_3d(float x, float y, float z, float size, float line_width) {
    (void)line_width; // Sem equivalente confiavel na raylib (depende de driver) - best-effort.
    Vector3 pos{x, y, z};
    Vector3 sz{size, size, size};
    DrawCubeWiresV(pos, sz, color_f(0.0f, 0.0f, 0.0f, 0.8f));
}

// Renderizar um cubo no espaco 3D com iluminacao simples (Minicraft style). Fica manual (rlgl):
// grava sombreamento distinto por face (topo/lado/lado escuro), o que nenhuma funcao de alto
// nivel da raylib (DrawCube usa uma cor unica) consegue expressar. Porte mecanico de
// glBegin(GL_QUADS)/glVertex3f/glColor4f/glEnd para rlBegin(RL_QUADS)/rlVertex3f/rlColor4f/rlEnd.
void render_cube_3d(float x, float y, float z, float size, float r, float g, float b, float a, bool outline) {
    float half = size * 0.5f;

    // Cores com sombreamento por face (iluminacao fake - Minicraft tem 3 niveis)
    float top_shade = 1.0f;      // Face superior - clara
    float side_shade = 0.70f;    // Faces laterais - media
    float dark_shade = 0.50f;    // Faces escuras

    float fog_r = r, fog_g = g, fog_b = b;
    apply_frame_fog(x, y, z, fog_r, fog_g, fog_b);
    r = fog_r; g = fog_g; b = fog_b;

    rlBegin(RL_QUADS);

    // Face superior (Y+) - mais clara
    rlColor4f(r * top_shade, g * top_shade, b * top_shade, a);
    rlVertex3f(x - half, y + half, z - half);
    rlVertex3f(x + half, y + half, z - half);
    rlVertex3f(x + half, y + half, z + half);
    rlVertex3f(x - half, y + half, z + half);

    // Face inferior (Y-) - escura (normalmente nao visivel)
    rlColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    rlVertex3f(x - half, y - half, z + half);
    rlVertex3f(x + half, y - half, z + half);
    rlVertex3f(x + half, y - half, z - half);
    rlVertex3f(x - half, y - half, z - half);

    // Face frontal (Z+) - media
    rlColor4f(r * side_shade, g * side_shade, b * side_shade, a);
    rlVertex3f(x - half, y - half, z + half);
    rlVertex3f(x + half, y - half, z + half);
    rlVertex3f(x + half, y + half, z + half);
    rlVertex3f(x - half, y + half, z + half);

    // Face traseira (Z-) - escura
    rlColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    rlVertex3f(x + half, y - half, z - half);
    rlVertex3f(x - half, y - half, z - half);
    rlVertex3f(x - half, y + half, z - half);
    rlVertex3f(x + half, y + half, z - half);

    // Face direita (X+) - media
    rlColor4f(r * side_shade, g * side_shade, b * side_shade, a);
    rlVertex3f(x + half, y - half, z + half);
    rlVertex3f(x + half, y - half, z - half);
    rlVertex3f(x + half, y + half, z - half);
    rlVertex3f(x + half, y + half, z + half);

    // Face esquerda (X-) - escura
    rlColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    rlVertex3f(x - half, y - half, z - half);
    rlVertex3f(x - half, y - half, z + half);
    rlVertex3f(x - half, y + half, z + half);
    rlVertex3f(x - half, y + half, z - half);

    rlEnd();

    // Desenhar outline se solicitado (estilo pixel art)
    if (outline) {
        render_cube_outline_3d(x, y, z, size, 1.0f);
    }
}

// render_sphere_3d removed (raylib migration): confirmed dead code before removal (zero call
// sites anywhere in src/, only a stale comment referenced it).

// Renderizar parede vertical texturizada (para laterais do terreno em altura). Fica manual
// (rlgl): geometria de UV customizado por face (parede extrudada entre duas alturas,
// eixo/sinal parametrizado) que nenhum primitivo de alto nivel da raylib cobre. Porte mecanico
// da mesma logica de switch por face, so trocando glBegin/glVertex3f/glColor4f/glTexCoord2f/
// glEnd por rlBegin(RL_QUADS)/rlVertex3f/rlColor4f/rlTexCoord2f/rlEnd.
void render_wall_3d_tex(WallFace face, float x, float z, float y0, float y1, Tile tile,
                         float tint_r, float tint_g, float tint_b, float a, float shade) {
    if (y1 <= y0) return;
    constexpr float half = 0.5f;
    UvRect uv = atlas_uv(tile);

    float r = tint_r * shade, g = tint_g * shade, b = tint_b * shade;
    apply_frame_fog(x, (y0 + y1) * 0.5f, z, r, g, b);

    rlColor4f(r, g, b, a);
    rlBegin(RL_QUADS);
    switch (face) {
        case WallFace::XPos: {
            // Original: render_wall_3d_tex_xpos
            float xf = x + half;
            float z0 = z - half;
            float z1 = z + half;
            rlTexCoord2f(uv.u0, uv.v0); rlVertex3f(xf, y0, z0);
            rlTexCoord2f(uv.u1, uv.v0); rlVertex3f(xf, y0, z1);
            rlTexCoord2f(uv.u1, uv.v1); rlVertex3f(xf, y1, z1);
            rlTexCoord2f(uv.u0, uv.v1); rlVertex3f(xf, y1, z0);
            break;
        }
        case WallFace::XNeg: {
            // Original: render_wall_3d_tex_xneg
            float xf = x - half;
            float z0 = z - half;
            float z1 = z + half;
            rlTexCoord2f(uv.u0, uv.v0); rlVertex3f(xf, y0, z1);
            rlTexCoord2f(uv.u1, uv.v0); rlVertex3f(xf, y0, z0);
            rlTexCoord2f(uv.u1, uv.v1); rlVertex3f(xf, y1, z0);
            rlTexCoord2f(uv.u0, uv.v1); rlVertex3f(xf, y1, z1);
            break;
        }
        case WallFace::ZPos: {
            // Original: render_wall_3d_tex_zpos
            float zf = z + half;
            float x0 = x - half;
            float x1 = x + half;
            rlTexCoord2f(uv.u0, uv.v0); rlVertex3f(x0, y0, zf);
            rlTexCoord2f(uv.u1, uv.v0); rlVertex3f(x1, y0, zf);
            rlTexCoord2f(uv.u1, uv.v1); rlVertex3f(x1, y1, zf);
            rlTexCoord2f(uv.u0, uv.v1); rlVertex3f(x0, y1, zf);
            break;
        }
        case WallFace::ZNeg: {
            // Original: render_wall_3d_tex_zneg
            float zf = z - half;
            float x0 = x - half;
            float x1 = x + half;
            rlTexCoord2f(uv.u0, uv.v0); rlVertex3f(x1, y0, zf);
            rlTexCoord2f(uv.u1, uv.v0); rlVertex3f(x0, y0, zf);
            rlTexCoord2f(uv.u1, uv.v1); rlVertex3f(x0, y1, zf);
            rlTexCoord2f(uv.u0, uv.v1); rlVertex3f(x1, y1, zf);
            break;
        }
    }
    rlEnd();
}
