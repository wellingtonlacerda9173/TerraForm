#include "world.h"

#include "math_core.h"   // clamp01, smoothstep01, kHeightScale
#include "noise.h"       // init_permutation, perlin, fbm, ridged_fbm, lerp
#include "config_types.h" // TerrainConfig, MiningConfig (types of the extern globals below)
#include "game_state.h"  // rng_next_u32, rng_next_f01, set_toast

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Globais de estado de jogo ainda definidas em main.cpp (extracao completa para
// game_state/config_io/camera/player_physics e uma fase posterior ou paralela do plano de
// refatoracao). Removido o "static" delas em main.cpp para dar linkage externo, ja que
// World::gen()/update_phase()/terraform_step()/recompute_terraform_score()/
// melt_ice_around() abaixo precisam le-las/escreve-las de outra unidade de traducao.
// Mesmo padrao de g_oxygen/g_water_res/g_temperature/g_atmosphere em textures.cpp e de
// g_terrain_cfg/g_mining_cfg em config_io.cpp.
extern TerrainConfig g_terrain_cfg;
extern MiningConfig g_mining_cfg;
extern TerraPhase g_phase;
extern float g_terraform;
extern bool g_victory;
extern float g_co2_level;
extern bool g_surface_dirty;
extern float g_temperature;
extern float g_atmosphere;
extern float g_oxygen;
extern float g_water_res;

// Limiares de temperatura usados por update_phase()/melt_ice_around() abaixo.
// kTempHabitable/kTempTarget vieram de main.cpp (eram "static constexpr float" la, so
// usados por update_phase(), que se mudou para ca). kTempThawing tambem era static la,
// mas main.cpp ainda tem sua propria copia identica (o timer global de degelo em
// update_modules() a usa diretamente) - como e um literal em tempo de compilacao, e nao
// estado mutavel, duplicar a constante file-local em vez de compartilhar via extern.
static constexpr float kTempThawing = 0.0f;     // Water can be liquid
static constexpr float kTempHabitable = 15.0f;  // Can plant outside
static constexpr float kTempTarget = 22.0f;     // Ideal Earth-like

// O unico World do jogo (ver comentario em world.h).
World* g_world = nullptr;

// ============= WORLD GENERATION (Macro Heightmap + Erosion + Biomes) =============
// Extraido verbatim de main.cpp (era um metodo inline "void gen() { ... }" dentro da
// struct World); corpo inalterado, so a assinatura virou out-of-line.
void World::gen() {
    init_permutation(seed);

    std::fill(tiles.begin(), tiles.end(), Block::Air);
    std::fill(ground.begin(), ground.end(), Block::Dirt);
    std::fill(heightmap.begin(), heightmap.end(), 0);
    std::fill(surface_y.begin(), surface_y.end(), 0);

    const TerrainConfig& cfg = g_terrain_cfg;
    auto index_of = [this](int x, int y) -> size_t { return (size_t)y * (size_t)w + (size_t)x; };

    int min_h_i = std::max(0, (int)std::lround(cfg.min_height));
    int max_h_i = std::max(min_h_i + 2, (int)std::lround(cfg.max_height));
    int sea_h = std::clamp((int)std::lround(cfg.sea_height), min_h_i, max_h_i - 1);
    int snow_h = std::clamp((int)std::lround(cfg.snow_height), sea_h + 2, max_h_i);
    sea_level = sea_h;

    const size_t cell_count = (size_t)w * (size_t)h;
    std::vector<float> heights(cell_count, 0.0f);
    std::vector<float> temp_map(cell_count, 0.0f);
    std::vector<float> moist_map(cell_count, 0.0f);
    std::vector<float> ridge_map(cell_count, 0.0f);
    std::vector<float> valley_map(cell_count, 0.0f);
    std::vector<uint8_t> biome_map(cell_count, 0);

    // === Passo 1: macro shape (continentes, bacias, vales, cordilheiras) ===
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float fx = (float)x;
            float fy = (float)y;

            float warp_x = (fbm(fx * cfg.warp_scale + 41.0f, fy * cfg.warp_scale - 63.0f, 3) - 0.5f) * 2.0f;
            float warp_y = (fbm(fx * cfg.warp_scale - 97.0f, fy * cfg.warp_scale + 29.0f, 3) - 0.5f) * 2.0f;
            float wx = fx + warp_x * cfg.warp_strength;
            float wy = fy + warp_y * cfg.warp_strength;

            float macro = fbm(wx * cfg.macro_scale, wy * cfg.macro_scale, 6);
            float basin = 1.0f - fbm(wx * (cfg.macro_scale * 1.55f) + 1400.0f,
                                     wy * (cfg.macro_scale * 1.55f) + 1400.0f, 4);
            float ridge = ridged_fbm(wx * cfg.ridge_scale + 700.0f, wy * cfg.ridge_scale + 700.0f, 5);
            float valley = 1.0f - ridged_fbm(wx * cfg.valley_scale + 2500.0f, wy * cfg.valley_scale + 2500.0f, 4);
            float detail = fbm(wx * cfg.detail_scale + 3100.0f, wy * cfg.detail_scale + 3100.0f, 4);
            float hills = fbm(wx * (cfg.detail_scale * 0.52f) + 900.0f,
                              wy * (cfg.detail_scale * 0.52f) + 900.0f, 3);

            float mountain_w = smoothstep01(0.56f, 0.90f, ridge) * smoothstep01(0.38f, 0.88f, macro);
            float valley_w = smoothstep01(0.52f, 0.92f, valley) * (1.0f - mountain_w * 0.58f);
            float plateau_w = smoothstep01(cfg.plateau_level - 0.10f, cfg.plateau_level + 0.12f, macro) *
                              smoothstep01(0.35f, 0.74f, hills) * (1.0f - mountain_w * 0.75f);
            float plains_w = clamp01(1.0f - mountain_w - valley_w * 0.72f - plateau_w * 0.48f);

            float plains_h = 0.30f + (macro - 0.5f) * 0.12f + (hills - 0.5f) * 0.11f + (detail - 0.5f) * 0.07f;
            float valley_h = 0.24f + (macro - 0.5f) * 0.08f + (detail - 0.5f) * 0.05f - valley_w * 0.23f - basin * 0.08f;
            float mountain_h = 0.42f + std::pow(ridge, 1.85f) * 0.60f + (hills - 0.5f) * 0.08f;
            float plateau_h = 0.52f + std::pow(macro, 1.15f) * 0.30f + (detail - 0.5f) * 0.04f;
            plateau_h = lerp(plateau_h, std::floor(plateau_h * 9.0f) / 9.0f, cfg.plateau_flatten);

            float wsum = plains_w + valley_w + mountain_w + plateau_w + 0.0001f;
            float hn = (plains_h * plains_w + valley_h * valley_w + mountain_h * mountain_w + plateau_h * plateau_w) / wsum;
            hn += (macro - 0.5f) * cfg.macro_weight * 0.22f;
            hn += (ridge - 0.5f) * cfg.ridge_weight * 0.18f;
            hn -= valley_w * cfg.valley_weight * 0.15f;
            hn += (detail - 0.5f) * cfg.detail_weight;

            // Fendas e crateras suaves (antes da erosao para ficar natural).
            float fissure_line = std::fabs(perlin(wx * cfg.fissure_scale + 4300.0f, wy * cfg.fissure_scale + 4300.0f) - 0.5f);
            float fissure_cut = clamp01((0.018f - fissure_line) / 0.018f);
            float crater_shape = 1.0f - std::fabs(perlin(wx * cfg.crater_scale + 5200.0f, wy * cfg.crater_scale + 5200.0f) * 2.0f - 1.0f);
            float crater_core = smoothstep01(0.82f, 0.96f, crater_shape);
            float crater_rim = smoothstep01(0.62f, 0.80f, crater_shape) * (1.0f - crater_core);
            hn -= fissure_cut * cfg.fissure_depth;
            hn -= crater_core * cfg.crater_depth;
            hn += crater_rim * cfg.crater_depth * 0.42f;
            hn = clamp01(hn);

            float lat = 0.0f;
            if (h > 1) {
                float ny = (fy / (float)(h - 1)) * 2.0f - 1.0f;
                lat = std::fabs(ny);
            }

            float temp = fbm(wx * cfg.temp_scale + 900.0f, wy * cfg.temp_scale + 900.0f, 4);
            temp = clamp01(temp * 0.72f + (1.0f - lat) * 0.28f - hn * 0.38f);
            float moisture = fbm(wx * cfg.moisture_scale + 1300.0f, wy * cfg.moisture_scale + 1300.0f, 4);
            moisture = clamp01(moisture * 0.80f + basin * 0.20f);

            uint8_t biome = 0; // 0 Planicie | 1 Vale | 2 Montanha | 3 Plato | 4 Gelo
            if (hn > 0.72f && temp < 0.44f) biome = 4;
            else if (mountain_w >= valley_w && mountain_w >= plateau_w && mountain_w >= plains_w) biome = 2;
            else if (plateau_w >= valley_w && plateau_w >= plains_w) biome = 3;
            else if (valley_w >= plains_w) biome = 1;

            size_t idx = index_of(x, y);
            heights[idx] = hn;
            temp_map[idx] = temp;
            moist_map[idx] = moisture;
            ridge_map[idx] = ridge;
            valley_map[idx] = valley;
            biome_map[idx] = biome;
        }
    }

    // === Passo 2: erosao termica (remove "paredes") ===
    if (cfg.thermal_erosion_passes > 0) {
        std::vector<float> delta(cell_count, 0.0f);
        for (int pass = 0; pass < cfg.thermal_erosion_passes; ++pass) {
            std::fill(delta.begin(), delta.end(), 0.0f);
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    size_t i = index_of(x, y);
                    float h0 = heights[i];
                    const int nx[4] = {1, -1, 0, 0};
                    const int ny[4] = {0, 0, 1, -1};
                    for (int k = 0; k < 4; ++k) {
                        size_t j = index_of(x + nx[k], y + ny[k]);
                        float diff = h0 - heights[j];
                        if (diff > cfg.thermal_talus) {
                            float move = (diff - cfg.thermal_talus) * cfg.erosion_strength * 0.22f;
                            delta[i] -= move;
                            delta[j] += move;
                        }
                    }
                }
            }
            for (size_t i = 0; i < cell_count; ++i) {
                heights[i] = clamp01(heights[i] + delta[i]);
            }
        }
    }

    // === Passo 3: erosao hidrica simplificada (alarga vales/bacias) ===
    if (cfg.hydraulic_erosion_passes > 0) {
        std::vector<float> copy = heights;
        for (int pass = 0; pass < cfg.hydraulic_erosion_passes; ++pass) {
            copy = heights;
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    size_t i = index_of(x, y);
                    float center = copy[i];
                    float n = copy[index_of(x, y - 1)];
                    float s = copy[index_of(x, y + 1)];
                    float e = copy[index_of(x + 1, y)];
                    float wv = copy[index_of(x - 1, y)];
                    float ne = copy[index_of(x + 1, y - 1)];
                    float nw = copy[index_of(x - 1, y - 1)];
                    float se = copy[index_of(x + 1, y + 1)];
                    float sw = copy[index_of(x - 1, y + 1)];
                    float avg = (center * 2.0f + n + s + e + wv + ne + nw + se + sw) / 10.0f;
                    float min_n = std::min({center, n, s, e, wv, ne, nw, se, sw});
                    float slope = center - min_n;
                    float valley_boost = smoothstep01(0.60f, 0.95f, valley_map[i]) * 0.16f;
                    float blend = std::clamp(cfg.erosion_strength * (0.11f + slope * 1.1f) + valley_boost, 0.0f, 0.45f);
                    heights[i] = clamp01(lerp(center, avg, blend));
                }
            }
        }
    }

    // === Passo 4: suavizacao final das encostas ===
    if (cfg.smooth_passes > 0) {
        std::vector<float> copy = heights;
        for (int pass = 0; pass < cfg.smooth_passes; ++pass) {
            copy = heights;
            for (int y = 1; y < h - 1; ++y) {
                for (int x = 1; x < w - 1; ++x) {
                    size_t i = index_of(x, y);
                    float avg4 = (copy[index_of(x - 1, y)] + copy[index_of(x + 1, y)] +
                                  copy[index_of(x, y - 1)] + copy[index_of(x, y + 1)]) * 0.25f;
                    heights[i] = clamp01(lerp(copy[i], avg4, 0.15f + cfg.biome_blend * 0.18f));
                }
            }
        }
    }

    // === Passo 5: converter heightmap e definir solo por bioma ===
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = index_of(x, y);
            float hn = heights[i];
            int h_val = min_h_i + (int)std::lround(hn * (float)(max_h_i - min_h_i));
            int16_t th = (int16_t)std::clamp(h_val, min_h_i, max_h_i);
            set_height(x, y, th);

            float temp = temp_map[i];
            float moisture = moist_map[i];
            uint8_t biome = biome_map[i];

            Block g = Block::Dirt;
            if ((int)th <= sea_h) {
                g = (temp < 0.44f) ? Block::Ice : Block::Water;
            } else if (biome == 4 || (int)th >= snow_h || temp < 0.25f) {
                float snow_var = fbm((float)x * 0.045f + 7600.0f, (float)y * 0.045f + 7600.0f, 2);
                g = (snow_var > 0.56f) ? Block::Ice : Block::Snow;
            } else if (biome == 1 && moisture > 0.66f) {
                g = Block::Dirt; // vale mais umido
            } else if (moisture < 0.30f && temp > 0.52f) {
                g = Block::Sand;
            } else if (biome == 3 && moisture < 0.36f) {
                g = Block::Sand;
            } else {
                g = Block::Dirt;
            }

            set_ground(x, y, g);
            set(x, y, g);
        }
    }

    // === Passo 6: detalhamento (rochas, fendas, pedregulhos, minerios) ===
    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            Block g = get_ground(x, y);
            int16_t th = height_at(x, y);
            if ((int)th <= sea_h && g == Block::Water) continue;

            float fx = (float)x;
            float fy = (float)y;
            float ridge = ridge_map[index_of(x, y)];

            float h_c = (float)height_at(x, y);
            float h_e = (float)height_at(x + 1, y);
            float h_w = (float)height_at(x - 1, y);
            float h_n = (float)height_at(x, y - 1);
            float h_s = (float)height_at(x, y + 1);
            float slope = std::sqrt((h_e - h_w) * (h_e - h_w) + (h_s - h_n) * (h_s - h_n));

            float rock_n = fbm(fx * 0.060f + 2100.0f, fy * 0.060f + 2100.0f, 3);
            float boulder_n = fbm(fx * 0.022f + 3300.0f, fy * 0.022f + 3300.0f, 2);
            float fissure = std::fabs(perlin(fx * (cfg.fissure_scale * 1.65f) + 5200.0f,
                                            fy * (cfg.fissure_scale * 1.65f) + 5200.0f) - 0.5f);

            float obj_bias = rock_n + ridge * 0.55f + slope * 0.020f + cfg.detail_object_density;
            if (obj_bias > 1.30f || (boulder_n > 0.79f && slope > 2.1f)) {
                set(x, y, Block::Stone);
                continue;
            }

            float ore1 = fbm(fx * 0.11f + 200.0f, fy * 0.11f + 200.0f, 3);
            float ore2 = fbm(fx * 0.09f + 300.0f, fy * 0.09f + 300.0f, 3);
            float ore3 = fbm(fx * 0.14f + 400.0f, fy * 0.14f + 400.0f, 2);

            if (ore1 > 0.88f && (int)th > sea_h + 2) {
                set(x, y, Block::Iron);
            } else if (ore1 > 0.85f && (int)th > sea_h + 1) {
                set(x, y, Block::Coal);
            } else if (ore2 > 0.89f && (int)th > sea_h + 2) {
                set(x, y, Block::Copper);
            } else if (ore3 > 0.91f && (g == Block::Snow || (int)th > snow_h - 2)) {
                set(x, y, Block::Crystal);
            } else if (ore2 > 0.93f && ore3 > 0.93f) {
                set(x, y, Block::Metal);
            } else if (fissure < 0.014f && (int)th > sea_h + 3) {
                set(x, y, Block::Coal); // fendas escuras
            }

            if (get(x, y) == get_ground(x, y) && (int)th > sea_h + 1 && (int)th < snow_h - 2) {
                float moisture = moist_map[index_of(x, y)];
                float org = fbm(fx * 0.10f + 500.0f, fy * 0.10f + 500.0f, 2);
                if (moisture > 0.70f && org > 0.92f) {
                    set(x, y, Block::Organic);
                }
            }

            if (get(x, y) == get_ground(x, y)) {
                float dry = 1.0f - moist_map[index_of(x, y)];
                float tech = fbm(fx * 0.083f + 4200.0f, fy * 0.083f + 4200.0f, 2);
                if (dry > 0.60f && tech > 0.93f) {
                    set(x, y, Block::Components);
                }
            }
        }
    }

    rebuild_surface_cache();
}

// Altura adicional de um bloco acima do terreno (para colisao/ground height).
float get_block_height(Block b) {
    if (b == Block::Air) return 0.0f;
    if (is_ground_like(b)) return 0.0f; // Solo (inclui agua/gelo), sem volume acima
    if (b == Block::Leaves) return 0.0f; // Folhagem e tratada como plano

    // Objetos (rochas/minerios/modulos/estruturas): cubo 1x1x1 sobre o solo.
    // Se quiser modulos mais altos no futuro, troque por um box/prisma (nao cubo uniforme).
    if (is_module(b)) return 1.0f;
    if (is_base_structure(b)) return 1.0f;
    if (is_solid(b)) return 1.0f;
    return 0.0f;
}

Block surface_block_at(const World& world, int tx, int tz) {
    Block top = world.get(tx, tz);
    if (top != Block::Air && is_ground_like(top)) return top;
    return world.get_ground(tx, tz);
}

Block object_block_at(const World& world, int tx, int tz) {
    Block top = world.get(tx, tz);
    if (top != Block::Air && !is_ground_like(top)) return top;
    return Block::Air;
}

float surface_height_at(const World& world, int tx, int tz) {
    float h = (float)world.height_at(tx, tz) * kHeightScale;
    Block obj = object_block_at(world, tx, tz);
    if (obj != Block::Air) h += get_block_height(obj);
    return h;
}

bool is_mineable(Block b) {
    if (b == Block::Air || b == Block::Water) return false;
    if (is_base_structure(b)) return false;
    return true;
}

int block_hits_required(Block b) {
    switch (b) {
        case Block::Sand:
            return g_mining_cfg.hits_sand;
        case Block::Grass:
        case Block::Dirt:
        case Block::Organic:
        case Block::Leaves:
        case Block::BuildSlot:
            return g_mining_cfg.hits_dirt;
        case Block::Ice:
            return g_mining_cfg.hits_ice;
        case Block::Snow:
            return g_mining_cfg.hits_snow;
        case Block::Stone:
            return g_mining_cfg.hits_stone;
        case Block::Coal:
        case Block::Iron:
        case Block::Copper:
            return g_mining_cfg.hits_ore;
        case Block::Metal:
        case Block::Components:
            return g_mining_cfg.hits_metal;
        case Block::Crystal:
            return g_mining_cfg.hits_crystal;
        case Block::Wood:
            return g_mining_cfg.hits_wood;
        default:
            break;
    }
    if (is_module(b)) return g_mining_cfg.hits_modules;
    return std::max(2, g_mining_cfg.hits_stone);
}

// ============= Terraforming Simulation =============
void try_spawn_tree(World& world, int x, int y) {
    // Planeta inospito no comeco: so gera vegetacao em fase habitavel/terraformed.
    if (g_phase < TerraPhase::Habitable) return;
    if (x < 2 || x >= world.w - 2 || y < 2 || y >= world.h - 2) return;

    // Apenas em grama e sem objetos/estruturas/modulos.
    if (world.get_ground(x, y) != Block::Grass) return;
    if (is_base_structure(world.get_ground(x, y))) return;
    if (object_block_at(world, x, y) != Block::Air) return;

    // Evitar encostar em modulos/base (mantem legibilidade e evita conflito com construcoes).
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            Block t = world.get(x + dx, y + dy);
            if (is_module(t) || is_base_structure(t)) return;
        }
    }

    // Arvore simples estilo Minicraft: tronco (cubo) + copa (folhas em volta).
    world.set(x, y, Block::Wood);
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            if (std::abs(ox) + std::abs(oy) > 3) continue;
            int tx = x + ox;
            int ty = y + oy;
            if (!world.in_bounds(tx, ty)) continue;
            if (tx == x && ty == y) continue;

            Block cur = world.get(tx, ty);
            if (is_module(cur) || is_base_structure(cur)) continue;
            if (object_block_at(world, tx, ty) != Block::Air) continue;

            world.set(tx, ty, Block::Leaves);
        }
    }
}

void terraform_step(World& world, int cx, int cy) {
    int radius = 10;
    for (int i = 0; i < 3; ++i) {
        float ang = rng_next_f01() * 6.2831853f;
        float rr = rng_next_f01() * radius;
        int x = cx + (int)std::round(std::cos(ang) * rr);
        int y = cy + (int)std::round(std::sin(ang) * rr);
        if (!world.in_bounds(x, y)) continue;

        Block top = world.get(x, y);
        if (is_module(top) || is_base_structure(top)) continue;
        if (is_base_structure(world.get_ground(x, y))) continue;

        auto set_ground_surface = [&](int tx, int ty, Block nb) {
            world.set_ground(tx, ty, nb);
            Block t = world.get(tx, ty);
            if (t != Block::Air && is_ground_like(t) && !is_base_structure(t) && !is_module(t)) {
                world.set(tx, ty, nb);
            }
        };

        Block g = world.get_ground(x, y);

        if (g == Block::Sand && g_oxygen >= 12.0f && g_water_res >= 12.0f) {
            set_ground_surface(x, y, Block::Dirt);
            g_surface_dirty = true;
        } else if (g == Block::Dirt && g_phase >= TerraPhase::Habitable &&
            g_oxygen >= 28.0f && g_water_res >= 18.0f) {
            set_ground_surface(x, y, Block::Grass);
            g_surface_dirty = true;
        } else if (g == Block::Grass && g_phase >= TerraPhase::Habitable &&
            g_oxygen >= 45.0f && g_water_res >= 35.0f) {
            if ((rng_next_u32() % 100u) < 2u) {
                try_spawn_tree(world, x, y);
                g_surface_dirty = true;
            }
        }
    }
}

void recompute_terraform_score(World& world) {
    int grass_tiles = 0;
    int tree_tiles = 0;
    int water_tiles = 0;

    for (int y = 0; y < world.h; ++y) {
        for (int x = 0; x < world.w; ++x) {
            Block g = world.get_ground(x, y);
            if (g == Block::Grass) grass_tiles++;
            if (g == Block::Water) water_tiles++;

            Block obj = object_block_at(world, x, y);
            if (obj == Block::Wood) tree_tiles++;
        }
    }

    float total = (float)std::max(1, world.w * world.h);
    float grass = (float)grass_tiles / total;
    float trees = (float)tree_tiles / total;
    float water = (float)water_tiles / total;

    float base = grass * 60.0f + trees * 20.0f + water * 20.0f;
    float env = 0.4f + 0.6f * (0.5f * clamp01(g_oxygen / 100.0f) + 0.5f * clamp01(g_water_res / 100.0f));
    g_terraform = std::clamp(base * env, 0.0f, 100.0f);
    // Victory is no longer decided here: objectives.cpp's final milestone
    // (TerraformComplete, checked via g_phase == TerraPhase::Terraformed) is now the
    // single source of truth for g_victory, replacing this and update_phase()'s old
    // redundant, differently-thresholded check below.
}

void update_phase() {
    // Update terraforming phase based on temperature
    TerraPhase old_phase = g_phase;

    if (g_temperature >= kTempHabitable && g_atmosphere >= 60.0f) {
        g_phase = TerraPhase::Habitable;
    } else if (g_temperature >= kTempThawing) {
        g_phase = TerraPhase::Thawing;
    } else if (g_co2_level > 10.0f) {
        g_phase = TerraPhase::Warming;
    } else {
        g_phase = TerraPhase::Frozen;
    }

    // Terraformed is the final phase; objectives.cpp watches for it and sets g_victory
    // once (see recompute_terraform_score()'s comment above for why this replaced the
    // old inline g_victory/toast side effects here).
    if (g_temperature >= kTempTarget && g_atmosphere >= 80.0f && g_terraform >= 70.0f) {
        g_phase = TerraPhase::Terraformed;
    }

    // Notify phase changes
    if (old_phase != g_phase && !g_victory) {
        set_toast(std::string("Fase: ") + phase_name(g_phase), 4.0f);
    }
}

void melt_ice_around(World& world, int cx, int cy, int radius) {
    if (g_temperature < kTempThawing) return; // Too cold to melt

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx*dx + dy*dy > radius*radius) continue;
            int x = cx + dx;
            int y = cy + dy;
            if (!world.in_bounds(x, y)) continue;

            // Derreter gelo do SOLO (ground). O topo (tiles) pode estar Air/objeto.
            if (world.get_ground(x, y) == Block::Ice && !is_base_structure(world.get_ground(x, y))) {
                world.set_ground(x, y, Block::Water);
                Block t = world.get(x, y);
                if (t != Block::Air && is_ground_like(t) && !is_base_structure(t) && !is_module(t)) {
                    world.set(x, y, Block::Water);
                }
                g_surface_dirty = true;
            }
        }
    }
}
