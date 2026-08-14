#pragma once

#include "blocks.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// ============= World =============
// Extracted verbatim from main.cpp (struct World, original lines ~161-505). The small
// one-line accessors (in_bounds/get/set/get_ground/set_ground/height_at/set_height/
// rebuild_surface_cache) stay inline in the struct exactly as before. World::gen() (the
// heightmap/erosion/biome generator) is the one method converted to an out-of-line
// definition, declared here and defined in world.cpp — see the comment there for details;
// its body is unchanged from the original.
struct World {
    int w = 0;
    int h = 0;
    unsigned seed = 1337;
    int sea_level = 0;
    std::vector<Block> tiles;
    std::vector<Block> ground;
    std::vector<int16_t> heightmap; // altura do terreno por tile (0 = nivel base)
    std::vector<int> surface_y;

    World(int W, int H, unsigned s)
        : w(W)
        , h(H)
        , seed(s)
        , tiles((size_t)W * (size_t)H, Block::Air)
        , ground((size_t)W * (size_t)H, Block::Dirt)
        , heightmap((size_t)W * (size_t)H, 0)
        , surface_y((size_t)W, H / 2) {
        gen();
    }

    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }

    Block get(int x, int y) const {
        if (!in_bounds(x, y)) return Block::Stone;
        return tiles[(size_t)y * (size_t)w + (size_t)x];
    }

    void set(int x, int y, Block b) {
        if (!in_bounds(x, y)) return;
        tiles[(size_t)y * (size_t)w + (size_t)x] = b;
    }

    Block get_ground(int x, int y) const {
        if (!in_bounds(x, y)) return Block::Dirt;
        return ground[(size_t)y * (size_t)w + (size_t)x];
    }

    void set_ground(int x, int y, Block b) {
        if (!in_bounds(x, y)) return;
        ground[(size_t)y * (size_t)w + (size_t)x] = b;
    }

    int16_t height_at(int x, int y) const {
        if (!in_bounds(x, y)) return 0;
        return heightmap[(size_t)y * (size_t)w + (size_t)x];
    }

    void set_height(int x, int y, int16_t v) {
        if (!in_bounds(x, y)) return;
        heightmap[(size_t)y * (size_t)w + (size_t)x] = v;
    }

    void rebuild_surface_cache() {
        surface_y.assign((size_t)w, h - 1);
        for (int x = 0; x < w; ++x) {
            int sy = h - 1;
            for (int y = 0; y < h; ++y) {
                Block b = get(x, y);
                if (b != Block::Air && b != Block::Water && b != Block::Leaves) {
                    sy = y;
                    break;
                }
            }
            surface_y[(size_t)x] = sy;
        }
    }

    // ============= WORLD GENERATION (Macro Heightmap + Erosion + Biomes) =============
    void gen();
};

// O unico World do jogo. A definicao (nao-static) vive em world.cpp: como o tipo World
// agora e definido aqui, world.cpp e o dono natural do ponteiro global tambem (todos os
// usos de g_world continuam em main.cpp, que ganha esta extern via este header).
extern World* g_world;

// Altura adicional de um bloco acima do terreno (para colisao/ground height).
float get_block_height(Block b);

Block surface_block_at(const World& world, int tx, int tz);
Block object_block_at(const World& world, int tx, int tz);
float surface_height_at(const World& world, int tx, int tz);

bool is_mineable(Block b);
int block_hits_required(Block b);

// ============= Terraforming Simulation =============
void try_spawn_tree(World& world, int x, int y);
void terraform_step(World& world, int cx, int cy);
void recompute_terraform_score(World& world);
void update_phase();
void melt_ice_around(World& world, int cx, int cy, int radius);
