#include "platform.h"
#include "render_primitives.h"

#include "math_core.h"    // kPi, clamp01
#include "textures.h"     // Tile, UvRect, atlas_uv

#include <cmath>

// ============= Rendering Helpers =============
void render_quad(float x, float y, float w, float h, float r, float g, float b, float a) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

// Quad 2D texturizado (tile do atlas). Requer GL_TEXTURE_2D habilitado e g_tex_atlas bindado.
void render_quad_tex(float x, float y, float w, float h, Tile tile, float tint_r, float tint_g, float tint_b, float a) {
    UvRect uv = atlas_uv(tile);
    glColor4f(tint_r, tint_g, tint_b, a);
    glBegin(GL_QUADS);
    glTexCoord2f(uv.u0, uv.v1); glVertex2f(x, y);
    glTexCoord2f(uv.u1, uv.v1); glVertex2f(x + w, y);
    glTexCoord2f(uv.u1, uv.v0); glVertex2f(x + w, y + h);
    glTexCoord2f(uv.u0, uv.v0); glVertex2f(x, y + h);
    glEnd();
}

void render_bar(float x, float y, float w, float h, float pct, float r, float g, float b) {
    render_quad(x, y, w, h, 0.0f, 0.0f, 0.0f, 0.55f);
    render_quad(x + 2.0f, y + 2.0f, (w - 4.0f) * clamp01(pct), h - 4.0f, r, g, b, 0.92f);
}

// ============= Astronaut Rendering =============
void render_circle(float cx, float cy, float radius, float r, float g, float b, float a, int segments) {
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segments; ++i) {
        float angle = (float)i / (float)segments * 2.0f * kPi;
        glVertex2f(cx + std::cos(angle) * radius, cy + std::sin(angle) * radius);
    }
    glEnd();
}

void render_ellipse(float cx, float cy, float rx, float ry, float r, float g, float b, float a, int segments) {
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(cx, cy);
    for (int i = 0; i <= segments; ++i) {
        float angle = (float)i / (float)segments * 2.0f * kPi;
        glVertex2f(cx + std::cos(angle) * rx, cy + std::sin(angle) * ry);
    }
    glEnd();
}

void render_rounded_rect(float x, float y, float w, float h, float radius, float r, float g, float b, float a) {
    // Simple rounded rectangle approximation
    render_quad(x + radius, y, w - 2*radius, h, r, g, b, a);
    render_quad(x, y + radius, w, h - 2*radius, r, g, b, a);
    render_circle(x + radius, y + radius, radius, r, g, b, a, 8);
    render_circle(x + w - radius, y + radius, radius, r, g, b, a, 8);
    render_circle(x + radius, y + h - radius, radius, r, g, b, a, 8);
    render_circle(x + w - radius, y + h - radius, radius, r, g, b, a, 8);
}

// ============= Renderizacao 3D (Estilo Minicraft) =============

// Renderizar outline de um cubo (bordas pretas estilo pixel art)
void render_cube_outline_3d(float x, float y, float z, float size, float line_width) {
    float half = size * 0.5f;

    glLineWidth(line_width);
    glColor4f(0.0f, 0.0f, 0.0f, 0.8f);

    // Arestas superiores
    glBegin(GL_LINE_LOOP);
    glVertex3f(x - half, y + half, z - half);
    glVertex3f(x + half, y + half, z - half);
    glVertex3f(x + half, y + half, z + half);
    glVertex3f(x - half, y + half, z + half);
    glEnd();

    // Arestas inferiores
    glBegin(GL_LINE_LOOP);
    glVertex3f(x - half, y - half, z - half);
    glVertex3f(x + half, y - half, z - half);
    glVertex3f(x + half, y - half, z + half);
    glVertex3f(x - half, y - half, z + half);
    glEnd();

    // Arestas verticais
    glBegin(GL_LINES);
    glVertex3f(x - half, y - half, z - half);
    glVertex3f(x - half, y + half, z - half);
    glVertex3f(x + half, y - half, z - half);
    glVertex3f(x + half, y + half, z - half);
    glVertex3f(x + half, y - half, z + half);
    glVertex3f(x + half, y + half, z + half);
    glVertex3f(x - half, y - half, z + half);
    glVertex3f(x - half, y + half, z + half);
    glEnd();
}

// Renderizar um cubo no espaco 3D com iluminacao simples (Minicraft style)
void render_cube_3d(float x, float y, float z, float size, float r, float g, float b, float a, bool outline) {
    float half = size * 0.5f;

    // Cores com sombreamento por face (iluminacao fake - Minicraft tem 3 niveis)
    float top_shade = 1.0f;      // Face superior - clara
    float side_shade = 0.70f;    // Faces laterais - media
    float dark_shade = 0.50f;    // Faces escuras

    glBegin(GL_QUADS);

    // Face superior (Y+) - mais clara
    glColor4f(r * top_shade, g * top_shade, b * top_shade, a);
    glVertex3f(x - half, y + half, z - half);
    glVertex3f(x + half, y + half, z - half);
    glVertex3f(x + half, y + half, z + half);
    glVertex3f(x - half, y + half, z + half);

    // Face inferior (Y-) - escura (normalmente nao visivel)
    glColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    glVertex3f(x - half, y - half, z + half);
    glVertex3f(x + half, y - half, z + half);
    glVertex3f(x + half, y - half, z - half);
    glVertex3f(x - half, y - half, z - half);

    // Face frontal (Z+) - media
    glColor4f(r * side_shade, g * side_shade, b * side_shade, a);
    glVertex3f(x - half, y - half, z + half);
    glVertex3f(x + half, y - half, z + half);
    glVertex3f(x + half, y + half, z + half);
    glVertex3f(x - half, y + half, z + half);

    // Face traseira (Z-) - escura
    glColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    glVertex3f(x + half, y - half, z - half);
    glVertex3f(x - half, y - half, z - half);
    glVertex3f(x - half, y + half, z - half);
    glVertex3f(x + half, y + half, z - half);

    // Face direita (X+) - media
    glColor4f(r * side_shade, g * side_shade, b * side_shade, a);
    glVertex3f(x + half, y - half, z + half);
    glVertex3f(x + half, y - half, z - half);
    glVertex3f(x + half, y + half, z - half);
    glVertex3f(x + half, y + half, z + half);

    // Face esquerda (X-) - escura
    glColor4f(r * dark_shade, g * dark_shade, b * dark_shade, a);
    glVertex3f(x - half, y - half, z - half);
    glVertex3f(x - half, y - half, z + half);
    glVertex3f(x - half, y + half, z + half);
    glVertex3f(x - half, y + half, z - half);

    glEnd();

    // Desenhar outline se solicitado (estilo pixel art)
    if (outline) {
        render_cube_outline_3d(x, y, z, size, 1.0f);
    }
}

// Renderizar esfera 3D simples (para player)
void render_sphere_3d(float cx, float cy, float cz, float radius, float r, float g, float b, float a, int segments) {
    // Aproximacao com faixas horizontais
    for (int i = 0; i < segments; ++i) {
        float lat0 = kPi * (-0.5f + (float)i / segments);
        float lat1 = kPi * (-0.5f + (float)(i + 1) / segments);
        float y0 = std::sin(lat0);
        float y1 = std::sin(lat1);
        float r0 = std::cos(lat0);
        float r1 = std::cos(lat1);

        // Sombreamento baseado na altura
        float shade = 0.6f + 0.4f * ((float)i / segments);
        glColor4f(r * shade, g * shade, b * shade, a);

        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= segments; ++j) {
            float lng = 2.0f * kPi * (float)j / segments;
            float x = std::cos(lng);
            float z = std::sin(lng);

            glVertex3f(cx + radius * x * r1, cy + radius * y1, cz + radius * z * r1);
            glVertex3f(cx + radius * x * r0, cy + radius * y0, cz + radius * z * r0);
        }
        glEnd();
    }
}

// Renderizar parede vertical texturizada (para laterais do terreno em altura). Requer
// GL_TEXTURE_2D habilitado. Unificacao das 4 funcoes originais render_wall_3d_tex_{xpos,
// xneg,zpos,zneg} num switch por face - cada `case` abaixo e uma transcricao exata do corpo
// da funcao original correspondente (mesmas variaveis xf/zf/x0/x1/z0/z1, mesma ordem de
// glTexCoord2f/glVertex3f, mesmo winding), so trocando o nome da funcao pelo case do enum.
void render_wall_3d_tex(WallFace face, float x, float z, float y0, float y1, Tile tile,
                         float tint_r, float tint_g, float tint_b, float a, float shade) {
    if (y1 <= y0) return;
    constexpr float half = 0.5f;
    UvRect uv = atlas_uv(tile);
    glColor4f(tint_r * shade, tint_g * shade, tint_b * shade, a);
    glBegin(GL_QUADS);
    switch (face) {
        case WallFace::XPos: {
            // Original: render_wall_3d_tex_xpos
            float xf = x + half;
            float z0 = z - half;
            float z1 = z + half;
            glTexCoord2f(uv.u0, uv.v0); glVertex3f(xf, y0, z0);
            glTexCoord2f(uv.u1, uv.v0); glVertex3f(xf, y0, z1);
            glTexCoord2f(uv.u1, uv.v1); glVertex3f(xf, y1, z1);
            glTexCoord2f(uv.u0, uv.v1); glVertex3f(xf, y1, z0);
            break;
        }
        case WallFace::XNeg: {
            // Original: render_wall_3d_tex_xneg
            float xf = x - half;
            float z0 = z - half;
            float z1 = z + half;
            glTexCoord2f(uv.u0, uv.v0); glVertex3f(xf, y0, z1);
            glTexCoord2f(uv.u1, uv.v0); glVertex3f(xf, y0, z0);
            glTexCoord2f(uv.u1, uv.v1); glVertex3f(xf, y1, z0);
            glTexCoord2f(uv.u0, uv.v1); glVertex3f(xf, y1, z1);
            break;
        }
        case WallFace::ZPos: {
            // Original: render_wall_3d_tex_zpos
            float zf = z + half;
            float x0 = x - half;
            float x1 = x + half;
            glTexCoord2f(uv.u0, uv.v0); glVertex3f(x0, y0, zf);
            glTexCoord2f(uv.u1, uv.v0); glVertex3f(x1, y0, zf);
            glTexCoord2f(uv.u1, uv.v1); glVertex3f(x1, y1, zf);
            glTexCoord2f(uv.u0, uv.v1); glVertex3f(x0, y1, zf);
            break;
        }
        case WallFace::ZNeg: {
            // Original: render_wall_3d_tex_zneg
            float zf = z - half;
            float x0 = x - half;
            float x1 = x + half;
            glTexCoord2f(uv.u0, uv.v0); glVertex3f(x1, y0, zf);
            glTexCoord2f(uv.u1, uv.v0); glVertex3f(x0, y0, zf);
            glTexCoord2f(uv.u1, uv.v1); glVertex3f(x0, y1, zf);
            glTexCoord2f(uv.u0, uv.v1); glVertex3f(x1, y1, zf);
            break;
        }
    }
    glEnd();
}
