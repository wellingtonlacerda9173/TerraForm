#include "player_physics.h"

#include "camera.h"       // reset_camera_near_player
#include "config_types.h" // PhysicsConfig, MiniMapRuntime (types of extern globals below)
#include "game_state.h"   // set_toast, UnlockProgress, Alert (types of extern globals below)
#include "noise.h"        // lerp
#include "world.h"        // World, g_world, surface_height_at, object_block_at, surface_block_at, get_block_height
#include "blocks.h"       // Block, is_solid, is_ground_like, TerraPhase, kBlockTypeCount
#include "modules_building.h"   // ConstructionJob, g_construction_queue, generate_base
#include "inventory_crafting.h" // g_inventory, g_selected

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// ConstructionJob/generate_base/g_construction_queue now come from modules_building.h
// (included above), and g_inventory/g_selected from inventory_crafting.h (also included
// above) - both real headers now that the items_particles/modules_building/
// inventory_crafting extraction stage exists. spawn_player_new_game (below) used to call
// a clear_construction_queue() wrapper defined in main.cpp, needed back when this file
// only had a forward declaration of the (then still main.cpp-owned) incomplete
// ConstructionJob type, for which std::vector<T>::clear() is not among the operations the
// standard guarantees to work with an incomplete T. Now that ConstructionJob is a
// complete type here (via modules_building.h), spawn_player_new_game calls
// g_construction_queue.clear() directly instead, and that wrapper is gone from main.cpp.
// Similarly, this file's own raw "extern std::array<int, kBlockTypeCount> g_inventory;"/
// "extern Block g_selected;" and "void generate_base(World& world);" forward declarations
// are gone too, replaced by the real declarations from the headers above.

// Globais de estado de jogo ainda definidas em main.cpp (dono continua sendo main.cpp
// ate as fases futuras de extracao: game_state para os recursos de base/jogador).
// Todas perderam o "static" que tinham em main.cpp porque spawn_player_new_game/
// respawn_player_at_base/spawn_player_at_base (abaixo) agora as leem/escrevem de outra
// unidade de traducao - mesmo padrao de g_oxygen/g_water_res em textures.cpp e
// g_terrain_cfg em world.cpp/config_io.cpp.
extern float g_base_energy;
extern float g_base_water;
extern float g_base_oxygen;
extern float g_base_food;
extern float g_base_integrity;
extern std::vector<Alert> g_alerts;
extern float g_player_oxygen;
extern float g_player_water;
extern float g_player_food;
extern float g_energy;   // Deprecated, use g_base_energy
extern float g_food;     // Deprecated, maps to g_player_food
extern UnlockProgress g_unlocks;
extern int g_base_x;
extern int g_base_y;
extern bool g_show_build_menu;
extern int g_build_menu_selection;
extern MiniMapRuntime g_minimap;

// Estas ja eram nao-static em main.cpp (extraidas/expostas em fases anteriores);
// so precisamos da declaracao aqui tambem, mesmo padrao de g_physics_cfg em camera.cpp.
extern float g_water_res;
extern float g_oxygen;
extern float g_terraform;
extern bool g_victory;
extern float g_temperature;
extern float g_co2_level;
extern float g_atmosphere;
extern TerraPhase g_phase;
extern PhysicsConfig g_physics_cfg;

// ============= Player =============
Player g_player;

// ============= Physics Runtime =============
PhysicsRuntime g_physics = {};

Vec2 get_player_render_pos() { return g_physics.render_pos; }
float get_player_render_y() { return g_physics.render_pos_y; }

// ============= Movement / Collision =============
float approach(float cur, float target, float max_delta) {
    float d = target - cur;
    if (d > max_delta) return cur + max_delta;
    if (d < -max_delta) return cur - max_delta;
    return target;
}

// Top-down: coloca jogador em posicao segura (walkable)
static void place_player_near(World& world, int x) {
    x = std::clamp(x, 0, world.w - 1);
    // Em top-down, usar centro do mapa em Y
    int y = world.h / 2;
    
    // Encontrar tile walkable proximo
    for (int radius = 0; radius < 20; ++radius) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                int tx = x + dx, ty = y + dy;
                if (tx < 1 || tx >= world.w - 1 || ty < 1 || ty >= world.h - 1) continue;
                if (!is_solid(world.get(tx, ty))) {
                    g_player.pos = {(float)tx, (float)ty};
                    g_player.vel = {0.0f, 0.0f};
                    g_player.pos_y = surface_height_at(world, tx, ty);
                    g_player.ground_height = g_player.pos_y;
                    g_player.on_ground = true;
                    reset_player_physics_runtime(true);
                    return;
                }
            }
        }
    }
    // Fallback
    g_player.pos = {(float)x, (float)y};
    g_player.vel = {0.0f, 0.0f};
    g_player.pos_y = surface_height_at(world, x, y);
    g_player.ground_height = g_player.pos_y;
    g_player.on_ground = true;
    reset_player_physics_runtime(true);
}

static int find_spawn_x(const World& world) {
    int mid = world.w / 2;
    for (int off = 0; off < world.w / 2; ++off) {
        for (int s = 0; s < 2; ++s) {
            int x = mid + (s == 0 ? off : -off);
            if (x < 0 || x >= world.w) continue;
            int sy = world.surface_y[(size_t)x];
            if (sy < world.sea_level - 2) return x;
        }
    }
    return mid;
}

void spawn_player_at_base() {
    // Spawn player at the base
    g_player.pos.x = (float)g_base_x;
    g_player.pos.y = (float)g_base_y;
    g_player.vel = {0.0f, 0.0f};
    g_player.vel_y = 0.0f;
    g_player.pos_y = 0.0f;
    if (g_world && g_world->in_bounds(g_base_x, g_base_y)) {
        g_player.pos_y = surface_height_at(*g_world, g_base_x, g_base_y);
    }
    g_player.on_ground = true;
    g_player.can_jump = true;
    g_player.ground_height = g_player.pos_y;
    g_player.facing_dir = 2; // Olhando para sul
    g_player.w = g_physics_cfg.collider_width;
    g_player.h = g_physics_cfg.collider_depth;
    reset_camera_near_player(true);
    reset_player_physics_runtime(true);
}

// Respawn automatico na base quando o jogador morre
void respawn_player_at_base(const char* death_reason) {
    spawn_player_at_base();
    
    // HP parcial apos respawn
    g_player.hp = 50;
    
    // Restaurar recursos parciais do traje
    g_player_oxygen = std::min(100.0f, g_player_oxygen + 50.0f);
    g_player_water = std::min(100.0f, g_player_water + 30.0f);
    g_player_food = std::min(100.0f, g_player_food + 20.0f);
    
    // Reabastecer jetpack
    g_player.jetpack_fuel = 50.0f;
    
    // Feedback visual
    char msg[128];
    snprintf(msg, sizeof(msg), "Resgatado na base! (%s)", death_reason);
    set_toast(msg, 4.0f);
    
    // Pequena penalidade nos recursos da base
    g_base_energy = std::max(0.0f, g_base_energy - 10.0f);
    g_base_oxygen = std::max(0.0f, g_base_oxygen - 5.0f);
}

void spawn_player_new_game(World& world) {
    // Generate base first (sets g_base_x and g_base_y)
    generate_base(world);
    
    // Spawn at base
    spawn_player_at_base();
    
    g_player.hp = 100;
    g_player.facing_dir = 2; // Olhando para sul

    // Starter kit - some resources to start building
    g_inventory.fill(0);
    g_inventory[(int)Block::Dirt] = 20;
    g_inventory[(int)Block::Stone] = 10;
    g_inventory[(int)Block::Iron] = 5;
    g_selected = Block::Dirt;
    
    // Player suit tanks - start full
    g_player_oxygen = 100.0f;
    g_player_water = 100.0f;
    g_player_food = 100.0f;
    
    // Base storage - start with some resources from the rocket
    g_base_energy = 100.0f;   // Solar panel starts generating
    g_base_water = 30.0f;     // Small water reserve
    g_base_oxygen = 50.0f;    // Some O2 from the rocket
    g_base_food = 40.0f;      // Emergency rations
    g_base_integrity = 100.0f;  // Base starts in perfect condition
    
    // Clear construction queue
    g_construction_queue.clear();
    g_alerts.clear();
    
    // Sync legacy variables
    g_energy = g_base_energy;
    g_water_res = g_player_water;
    g_oxygen = g_player_oxygen;
    g_food = g_player_food;
    
    // Reset terraforming state - frozen planet
    g_temperature = -60.0f;
    g_co2_level = 0.0f;
    g_atmosphere = 5.0f;  // Thin atmosphere
    g_terraform = 0.0f;
    g_phase = TerraPhase::Frozen;
    g_victory = false;
    
    // Progressive unlock system - only basic modules start unlocked
    g_unlocks = UnlockProgress{};
    g_unlocks.solar_unlocked = true;        // Começa com painel solar
    g_unlocks.water_extractor_unlocked = false;  // Desbloqueia ao coletar gelo
    g_unlocks.o2_generator_unlocked = false;     // Desbloqueia ao coletar ferro
    g_unlocks.greenhouse_unlocked = false;       // Desbloqueia ao ter agua
    g_unlocks.co2_factory_unlocked = false;      // Desbloqueia ao ter O2
    g_unlocks.habitat_unlocked = false;          // Desbloqueia ao ter estufa
    g_unlocks.terraformer_unlocked = false;      // Desbloqueia no final
    
    g_show_build_menu = false;
    g_build_menu_selection = 0;
    
    // Inicializar minimapa e fog of war
    size_t map_size = (size_t)world.w * (size_t)world.h;
    g_minimap.explored.clear();
    g_minimap.explored.resize(map_size, 0);  // Tudo inexplorado
    g_minimap.waypoints.clear();
    g_minimap.dirty_full = true;
    g_minimap.world_map_open = false;
    
    // Revelar area inicial ao redor da base
    int reveal_radius = 20;
    for (int dy = -reveal_radius; dy <= reveal_radius; ++dy) {
        for (int dx = -reveal_radius; dx <= reveal_radius; ++dx) {
            int x = g_base_x + dx;
            int y = g_base_y + dy;
            if (world.in_bounds(x, y)) {
                float dist = std::sqrt((float)(dx * dx + dy * dy));
                if (dist <= (float)reveal_radius) {
                    g_minimap.explored[(size_t)y * (size_t)world.w + (size_t)x] = 255;
                }
            }
        }
    }
}

// Colisao 3D - considera altura do jogador para permitir pular sobre blocos
struct TerrainPhysicsProfile {
    float speed_mult = 1.0f;
    float accel_mult = 1.0f;
    float decel_mult = 1.0f;
    float friction_mult = 1.0f;
    float slide_mult = 1.0f;
    const char* label = "Normal";
};

struct GroundProbeResult {
    bool has_hit = false;
    bool grounded = false;
    float height = 0.0f;
    Block surface = Block::Dirt;
    TerrainPhysicsType terrain = TerrainPhysicsType::Normal;
    Vec3 normal = {0.0f, 1.0f, 0.0f};
};

static TerrainPhysicsType terrain_type_from_block(Block b) {
    switch (b) {
        case Block::Ice:
            return TerrainPhysicsType::Ice;
        case Block::Sand:
            return TerrainPhysicsType::Sand;
        case Block::Stone:
        case Block::Coal:
        case Block::Iron:
        case Block::Copper:
        case Block::Crystal:
        case Block::Metal:
        case Block::Components:
            return TerrainPhysicsType::Stone;
        case Block::Organic:
            return TerrainPhysicsType::Mud;
        default:
            return TerrainPhysicsType::Normal;
    }
}

static TerrainPhysicsProfile terrain_profile_for(TerrainPhysicsType t, const PhysicsConfig& cfg) {
    TerrainPhysicsProfile p{};
    switch (t) {
        case TerrainPhysicsType::Ice:
            p.speed_mult = cfg.terrain_ice_speed;
            p.accel_mult = cfg.terrain_ice_accel;
            p.decel_mult = cfg.terrain_ice_accel;
            p.friction_mult = cfg.terrain_ice_friction;
            p.slide_mult = 1.45f;
            p.label = "Gelo";
            break;
        case TerrainPhysicsType::Sand:
            p.speed_mult = cfg.terrain_sand_speed;
            p.accel_mult = cfg.terrain_sand_accel;
            p.decel_mult = cfg.terrain_sand_accel;
            p.friction_mult = cfg.terrain_sand_friction;
            p.slide_mult = 0.90f;
            p.label = "Areia";
            break;
        case TerrainPhysicsType::Stone:
            p.speed_mult = cfg.terrain_stone_speed;
            p.accel_mult = cfg.terrain_stone_accel;
            p.decel_mult = cfg.terrain_stone_accel;
            p.friction_mult = cfg.terrain_stone_friction;
            p.slide_mult = 1.0f;
            p.label = "Pedra";
            break;
        case TerrainPhysicsType::Mud:
            p.speed_mult = cfg.terrain_mud_speed;
            p.accel_mult = cfg.terrain_mud_accel;
            p.decel_mult = cfg.terrain_mud_accel;
            p.friction_mult = cfg.terrain_mud_friction;
            p.slide_mult = 0.80f;
            p.label = "Lama";
            break;
        default:
            p.speed_mult = 1.0f;
            p.accel_mult = 1.0f;
            p.decel_mult = 1.0f;
            p.friction_mult = 1.0f;
            p.slide_mult = 1.0f;
            p.label = "Normal";
            break;
    }
    return p;
}

static float sample_heightmap_continuous(const World& world, float x, float z) {
    if (world.w <= 0 || world.h <= 0) return 0.0f;

    x = std::clamp(x, 0.0f, (float)world.w - 1.001f);
    z = std::clamp(z, 0.0f, (float)world.h - 1.001f);

    int x0 = (int)std::floor(x);
    int z0 = (int)std::floor(z);
    int x1 = std::min(world.w - 1, x0 + 1);
    int z1 = std::min(world.h - 1, z0 + 1);
    float tx = x - (float)x0;
    float tz = z - (float)z0;

    float h00 = (float)world.height_at(x0, z0) * kHeightScale;
    float h10 = (float)world.height_at(x1, z0) * kHeightScale;
    float h01 = (float)world.height_at(x0, z1) * kHeightScale;
    float h11 = (float)world.height_at(x1, z1) * kHeightScale;
    float hx0 = lerp(h00, h10, tx);
    float hx1 = lerp(h01, h11, tx);
    return lerp(hx0, hx1, tz);
}

static Vec3 compute_surface_normal(const World& world, float x, float z) {
    int tx = world_to_tile(x);
    int tz = world_to_tile(z);
    if (world.in_bounds(tx, tz) && object_block_at(world, tx, tz) != Block::Air) {
        return {0.0f, 1.0f, 0.0f};
    }

    float h_l = sample_heightmap_continuous(world, x - 0.45f, z);
    float h_r = sample_heightmap_continuous(world, x + 0.45f, z);
    float h_d = sample_heightmap_continuous(world, x, z - 0.45f);
    float h_u = sample_heightmap_continuous(world, x, z + 0.45f);
    Vec3 n = vec3_normalize({h_l - h_r, 0.90f, h_d - h_u});
    if (vec3_length(n) < 1e-5f) return {0.0f, 1.0f, 0.0f};
    return n;
}

static float sample_support_height(const World& world, float cx, float cz, float width, float depth, Block* out_surface = nullptr) {
    float hw = width * 0.5f;
    float hd = depth * 0.5f;
    const Vec2 samples[5] = {
        {0.0f, 0.0f},
        {-hw, -hd},
        {hw, -hd},
        {-hw, hd},
        {hw, hd},
    };

    float best_h = -10000.0f;
    Block best_surface = Block::Dirt;
    for (const Vec2& off : samples) {
        int tx = world_to_tile(cx + off.x);
        int tz = world_to_tile(cz + off.y);
        if (!world.in_bounds(tx, tz)) continue;
        float h = surface_height_at(world, tx, tz);
        if (h > best_h) {
            best_h = h;
            best_surface = surface_block_at(world, tx, tz);
        }
    }

    if (best_h <= -9999.0f) best_h = 0.0f;
    if (out_surface) *out_surface = best_surface;
    return best_h;
}

static bool column_blocks_movement(const World& world, int tx, int tz, float foot_y, float head_y, float step_allow, float& out_top) {
    if (!world.in_bounds(tx, tz)) {
        out_top = foot_y + step_allow + 10.0f;
        return true;
    }

    float terrain_h = (float)world.height_at(tx, tz) * kHeightScale;
    Block obj = object_block_at(world, tx, tz);
    float top_h = terrain_h;
    if (obj != Block::Air) top_h += get_block_height(obj);
    out_top = top_h;

    if (obj != Block::Air && !is_ground_like(obj)) {
        float block_bottom = terrain_h;
        float block_top = top_h;
        bool intersects_vertical = !(head_y <= block_bottom || foot_y >= block_top);
        if (intersects_vertical && (block_top > foot_y + step_allow + 1e-4f)) {
            return true;
        }
    }

    return top_h > foot_y + step_allow + 1e-4f;
}

static bool overlaps_blocking_volume(const Player& p, const World& world, const PhysicsConfig& cfg,
                                     float test_x, float test_z, float foot_y, float head_y) {
    float left = test_x - p.w * 0.5f + cfg.collision_skin;
    float right = test_x + p.w * 0.5f - cfg.collision_skin;
    float front = test_z - p.h * 0.5f + cfg.collision_skin;
    float back = test_z + p.h * 0.5f - cfg.collision_skin;

    int x0 = world_to_tile(left);
    int x1 = world_to_tile(right);
    int z0 = world_to_tile(front);
    int z1 = world_to_tile(back);

    for (int tz = z0; tz <= z1; ++tz) {
        for (int tx = x0; tx <= x1; ++tx) {
            float tile_top = 0.0f;
            if (!column_blocks_movement(world, tx, tz, foot_y, head_y, 0.0f, tile_top)) continue;
            float tile_l = tile_min(tx);
            float tile_r = tile_max(tx);
            float tile_f = tile_min(tz);
            float tile_b = tile_max(tz);
            if (right > tile_l && left < tile_r && back > tile_f && front < tile_b) return true;
        }
    }
    return false;
}

static bool try_step_climb(Player& p, const World& world, const PhysicsConfig& cfg, const Vec2& move_dir) {
    if (!p.on_ground) return false;
    if (vec2_length(move_dir) < 1e-5f) return false;

    Vec2 dir = vec2_normalize(move_dir);
    Vec2 perp = {-dir.y, dir.x};
    float lateral = p.w * 0.30f;
    float best_front_h = -10000.0f;

    for (int i = -1; i <= 1; ++i) {
        float sx = p.pos.x + dir.x * cfg.step_probe_distance + perp.x * lateral * (float)i;
        float sz = p.pos.y + dir.y * cfg.step_probe_distance + perp.y * lateral * (float)i;
        int tx = world_to_tile(sx);
        int tz = world_to_tile(sz);
        if (!world.in_bounds(tx, tz)) return false;
        float h = sample_support_height(world, sx, sz, p.w * 0.90f, p.h * 0.90f);
        best_front_h = std::max(best_front_h, h);
    }

    if (best_front_h <= -9999.0f) return false;
    float rise = best_front_h - p.pos_y;
    if (rise <= cfg.collision_skin) return false;
    if (rise > cfg.step_height + cfg.collision_skin) return false;

    float new_foot = best_front_h + cfg.collision_skin;
    float new_head = new_foot + cfg.collider_height;
    if (overlaps_blocking_volume(p, world, cfg, p.pos.x, p.pos.y, new_foot, new_head)) return false;

    p.pos_y = new_foot;
    p.ground_height = best_front_h;
    p.vel_y = std::max(0.0f, p.vel_y);
    g_physics.stepped = true;
    return true;
}

static void resolve_axis_collision(Player& p, const World& world, const PhysicsConfig& cfg,
                                   float move_amount, bool axis_x, const Vec2& move_dir) {
    if (move_amount == 0.0f) return;

    float skin = cfg.collision_skin;
    float foot_y = p.pos_y + skin;
    float head_y = p.pos_y + cfg.collider_height - skin;
    float step_allow = p.on_ground ? cfg.step_height : 0.05f;

    if (axis_x) {
        float front = p.pos.y - p.h * 0.5f + skin;
        float back = p.pos.y + p.h * 0.5f - skin;
        int z0 = world_to_tile(front);
        int z1 = world_to_tile(back);
        if (z1 < z0) z1 = z0;

        if (move_amount > 0.0f) {
            int tx = world_to_tile(p.pos.x + p.w * 0.5f);
            for (int tz = z0; tz <= z1; ++tz) {
                float tile_top = 0.0f;
                if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, tile_top)) continue;

                if (try_step_climb(p, world, cfg, move_dir)) {
                    foot_y = p.pos_y + skin;
                    head_y = p.pos_y + cfg.collider_height - skin;
                    float post_step_top = 0.0f;
                    if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, post_step_top)) continue;
                }

                p.pos.x = tile_min(tx) - p.w * 0.5f - skin;
                p.vel.x = 0.0f;
                g_physics.hit_x = true;
                g_physics.collision_normal = {-1.0f, 0.0f};
                break;
            }
        } else {
            int tx = world_to_tile(p.pos.x - p.w * 0.5f);
            for (int tz = z0; tz <= z1; ++tz) {
                float tile_top = 0.0f;
                if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, tile_top)) continue;

                if (try_step_climb(p, world, cfg, move_dir)) {
                    foot_y = p.pos_y + skin;
                    head_y = p.pos_y + cfg.collider_height - skin;
                    float post_step_top = 0.0f;
                    if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, post_step_top)) continue;
                }

                p.pos.x = tile_max(tx) + p.w * 0.5f + skin;
                p.vel.x = 0.0f;
                g_physics.hit_x = true;
                g_physics.collision_normal = {1.0f, 0.0f};
                break;
            }
        }
    } else {
        float left = p.pos.x - p.w * 0.5f + skin;
        float right = p.pos.x + p.w * 0.5f - skin;
        int x0 = world_to_tile(left);
        int x1 = world_to_tile(right);
        if (x1 < x0) x1 = x0;

        if (move_amount > 0.0f) {
            int tz = world_to_tile(p.pos.y + p.h * 0.5f);
            for (int tx = x0; tx <= x1; ++tx) {
                float tile_top = 0.0f;
                if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, tile_top)) continue;

                if (try_step_climb(p, world, cfg, move_dir)) {
                    foot_y = p.pos_y + skin;
                    head_y = p.pos_y + cfg.collider_height - skin;
                    float post_step_top = 0.0f;
                    if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, post_step_top)) continue;
                }

                p.pos.y = tile_min(tz) - p.h * 0.5f - skin;
                p.vel.y = 0.0f;
                g_physics.hit_z = true;
                g_physics.collision_normal = {0.0f, -1.0f};
                break;
            }
        } else {
            int tz = world_to_tile(p.pos.y - p.h * 0.5f);
            for (int tx = x0; tx <= x1; ++tx) {
                float tile_top = 0.0f;
                if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, tile_top)) continue;

                if (try_step_climb(p, world, cfg, move_dir)) {
                    foot_y = p.pos_y + skin;
                    head_y = p.pos_y + cfg.collider_height - skin;
                    float post_step_top = 0.0f;
                    if (!column_blocks_movement(world, tx, tz, foot_y, head_y, step_allow, post_step_top)) continue;
                }

                p.pos.y = tile_max(tz) + p.h * 0.5f + skin;
                p.vel.y = 0.0f;
                g_physics.hit_z = true;
                g_physics.collision_normal = {0.0f, 1.0f};
                break;
            }
        }
    }
}

// Corrige pequenas penetracoes residuais em paredes (drift/jitter acumulado).
static void depenetrate_player_horizontal(Player& p, const World& world, const PhysicsConfig& cfg) {
    const float skin = cfg.collision_skin;
    for (int iter = 0; iter < 5; ++iter) {
        float foot_y = p.pos_y + skin;
        float head_y = p.pos_y + cfg.collider_height - skin;

        float left = p.pos.x - p.w * 0.5f + skin;
        float right = p.pos.x + p.w * 0.5f - skin;
        float front = p.pos.y - p.h * 0.5f + skin;
        float back = p.pos.y + p.h * 0.5f - skin;

        int x0 = world_to_tile(left) - 1;
        int x1 = world_to_tile(right) + 1;
        int z0 = world_to_tile(front) - 1;
        int z1 = world_to_tile(back) + 1;

        bool has_overlap = false;
        float best_push = 0.0f;
        bool best_axis_x = true;
        float best_abs = std::numeric_limits<float>::infinity();

        for (int tz = z0; tz <= z1; ++tz) {
            for (int tx = x0; tx <= x1; ++tx) {
                float tile_top = 0.0f;
                if (!column_blocks_movement(world, tx, tz, foot_y, head_y, 0.0f, tile_top)) continue;

                float tile_l = tile_min(tx);
                float tile_r = tile_max(tx);
                float tile_f = tile_min(tz);
                float tile_b = tile_max(tz);

                float overlap_x = std::min(right, tile_r) - std::max(left, tile_l);
                float overlap_z = std::min(back, tile_b) - std::max(front, tile_f);
                if (overlap_x <= 0.0f || overlap_z <= 0.0f) continue;
                has_overlap = true;

                float push_x_neg = (tile_l - right) - skin;
                float push_x_pos = (tile_r - left) + skin;
                float push_z_neg = (tile_f - back) - skin;
                float push_z_pos = (tile_b - front) + skin;

                float cand_x = (std::fabs(push_x_neg) < std::fabs(push_x_pos)) ? push_x_neg : push_x_pos;
                float cand_z = (std::fabs(push_z_neg) < std::fabs(push_z_pos)) ? push_z_neg : push_z_pos;

                float abs_x = std::fabs(cand_x);
                float abs_z = std::fabs(cand_z);
                if (abs_x < best_abs) {
                    best_abs = abs_x;
                    best_push = cand_x;
                    best_axis_x = true;
                }
                if (abs_z < best_abs) {
                    best_abs = abs_z;
                    best_push = cand_z;
                    best_axis_x = false;
                }
            }
        }

        if (!has_overlap || !std::isfinite(best_abs) || best_abs <= 1e-6f) break;
        best_push = std::clamp(best_push, -0.35f, 0.35f);
        if (best_axis_x) {
            p.pos.x += best_push;
            p.vel.x = 0.0f;
            g_physics.hit_x = true;
        } else {
            p.pos.y += best_push;
            p.vel.y = 0.0f;
            g_physics.hit_z = true;
        }
    }
}

static void move_player_horizontal(Player& p, const World& world, const PhysicsConfig& cfg, const Vec2& world_delta, const Vec2& move_dir) {
    float max_component = std::max(std::fabs(world_delta.x), std::fabs(world_delta.y));
    int substeps = std::max(1, (int)std::ceil(max_component / std::max(0.05f, cfg.max_move_per_substep)));
    Vec2 step_delta = vec2_scale(world_delta, 1.0f / (float)substeps);

    for (int i = 0; i < substeps; ++i) {
        p.pos.x += step_delta.x;
        resolve_axis_collision(p, world, cfg, step_delta.x, true, move_dir);

        p.pos.y += step_delta.y;
        resolve_axis_collision(p, world, cfg, step_delta.y, false, move_dir);
    }

    p.pos.x = std::clamp(p.pos.x, 0.5f, (float)world.w - 1.5f);
    p.pos.y = std::clamp(p.pos.y, 0.5f, (float)world.h - 1.5f);
    depenetrate_player_horizontal(p, world, cfg);
}

static GroundProbeResult probe_ground(const Player& p, const World& world, const PhysicsConfig& cfg, bool capture_debug_rays) {
    GroundProbeResult result{};
    result.height = sample_support_height(world, p.pos.x, p.pos.y, p.w * 0.95f, p.h * 0.95f, &result.surface);

    float hw = p.w * 0.45f;
    float hd = p.h * 0.45f;
    const Vec2 offsets[5] = {
        {0.0f, 0.0f},
        {-hw, 0.0f},
        {hw, 0.0f},
        {0.0f, -hd},
        {0.0f, hd},
    };

    float ray_top = p.pos_y + cfg.ground_snap + 0.30f;
    float ray_bottom = p.pos_y - (cfg.step_height + cfg.ground_snap + 0.30f);
    float highest = -10000.0f;
    Block highest_block = Block::Dirt;
    Vec3 normal_accum = {0.0f, 0.0f, 0.0f};
    int hit_count = 0;

    if (capture_debug_rays) g_physics.debug_ray_count = 0;

    for (const Vec2& off : offsets) {
        float sx = p.pos.x + off.x;
        float sz = p.pos.y + off.y;
        int tx = world_to_tile(sx);
        int tz = world_to_tile(sz);
        bool in_bounds = world.in_bounds(tx, tz);
        float sample_h = in_bounds ? surface_height_at(world, tx, tz) : -10000.0f;
        bool hit = in_bounds && sample_h <= ray_top + cfg.ground_tolerance && sample_h >= ray_bottom;

        if (capture_debug_rays && g_physics.debug_ray_count < (int)g_physics.debug_rays.size()) {
            PhysicsRayDebug& dbg = g_physics.debug_rays[(size_t)g_physics.debug_ray_count++];
            dbg.from = {sx, ray_top, sz};
            dbg.to = {sx, hit ? sample_h : ray_bottom, sz};
            dbg.hit = hit;
        }

        if (!hit) continue;
        hit_count++;
        if (sample_h > highest) {
            highest = sample_h;
            highest_block = surface_block_at(world, tx, tz);
        }
        normal_accum = vec3_add(normal_accum, compute_surface_normal(world, sx, sz));
    }

    if (hit_count > 0) {
        result.has_hit = true;
        result.height = highest;
        result.surface = highest_block;
        result.normal = vec3_normalize(normal_accum);
        if (vec3_length(result.normal) < 1e-5f) result.normal = {0.0f, 1.0f, 0.0f};
        bool touching = p.pos_y <= result.height + cfg.ground_tolerance;
        bool snappable = (p.vel_y <= 0.0f) && (p.pos_y <= result.height + cfg.ground_snap);
        result.grounded = touching || snappable;
    } else {
        result.has_hit = false;
        result.grounded = false;
        result.normal = {0.0f, 1.0f, 0.0f};
    }

    result.terrain = terrain_type_from_block(result.surface);
    return result;
}

static float slope_speed_multiplier(const Vec3& normal, const Vec2& move_dir, const PhysicsConfig& cfg) {
    if (vec2_length(move_dir) < 1e-5f) return 1.0f;

    Vec2 uphill = vec2_normalize({-normal.x, -normal.z});
    if (vec2_length(uphill) < 1e-5f) return 1.0f;

    float along_uphill = vec2_dot(move_dir, uphill);
    float steepness = clamp01(1.0f - normal.y);
    if (along_uphill > 0.0f) {
        return lerp(1.0f, cfg.slope_uphill_speed_mult, steepness * along_uphill);
    }
    if (along_uphill < 0.0f) {
        return lerp(1.0f, cfg.slope_downhill_speed_mult, steepness * (-along_uphill));
    }
    return 1.0f;
}

static void apply_single_physics_step(const PlayerPhysicsInput& input, float fixed_dt) {
    if (!g_world) return;
    Player& p = g_player;
    const World& world = *g_world;
    const PhysicsConfig& cfg = g_physics_cfg;

    p.w = cfg.collider_width;
    p.h = cfg.collider_depth;

    g_physics.stepped = false;
    g_physics.hit_x = false;
    g_physics.hit_z = false;
    g_physics.sliding = false;
    g_physics.collision_normal = {0.0f, 0.0f};

    GroundProbeResult ground = probe_ground(p, world, cfg, true);
    p.on_ground = ground.grounded;
    p.ground_height = ground.height;
    g_physics.ground_normal = ground.normal;
    g_physics.terrain = ground.terrain;

    TerrainPhysicsProfile terrain = terrain_profile_for(ground.terrain, cfg);
    g_physics.terrain_name = terrain.label;

    if (p.on_ground) g_physics.coyote_timer = cfg.coyote_time;
    else g_physics.coyote_timer = std::max(0.0f, g_physics.coyote_timer - fixed_dt);

    if (input.jump_pressed) g_physics.jump_buffer_timer = cfg.jump_buffer;
    else g_physics.jump_buffer_timer = std::max(0.0f, g_physics.jump_buffer_timer - fixed_dt);

    bool consume_jump = (g_physics.jump_buffer_timer > 0.0f) && (g_physics.coyote_timer > 0.0f);
    if (consume_jump) {
        p.vel_y = cfg.jump_velocity;
        p.on_ground = false;
        g_physics.jump_buffer_timer = 0.0f;
        g_physics.coyote_timer = 0.0f;
    }

    if (input.jump_released && p.vel_y > 0.0f) {
        p.vel_y -= cfg.gravity * (cfg.jump_cancel_multiplier - 1.0f) * fixed_dt;
    }

    bool jetpack_now = !p.on_ground && input.jump_held && p.jetpack_fuel > 0.0f && p.vel_y <= cfg.jump_velocity * 0.60f;
    p.jetpack_active = jetpack_now;
    if (jetpack_now) {
        p.jetpack_fuel = std::max(0.0f, p.jetpack_fuel - cfg.jetpack_fuel_consume * fixed_dt);
        p.vel_y += cfg.jetpack_thrust * fixed_dt;
        p.vel_y = std::min(p.vel_y, cfg.jetpack_max_up_speed);
        p.jetpack_flame_anim += fixed_dt * 15.0f;
    } else if (p.on_ground) {
        p.jetpack_fuel = std::min(100.0f, p.jetpack_fuel + cfg.jetpack_fuel_regen * fixed_dt);
    }

    float gravity_mult = (p.vel_y < 0.0f) ? cfg.fall_multiplier : cfg.rise_multiplier;
    if (jetpack_now) gravity_mult *= cfg.jetpack_gravity_mult;
    p.vel_y -= cfg.gravity * gravity_mult * fixed_dt;
    p.vel_y = std::max(-cfg.terminal_velocity, p.vel_y);

    Vec2 move_dir = input.has_move ? vec2_normalize(input.move) : Vec2{0.0f, 0.0f};
    float slope_mult = slope_speed_multiplier(ground.normal, move_dir, cfg);
    float target_speed = cfg.max_speed * terrain.speed_mult * slope_mult * (input.run ? cfg.run_multiplier : 1.0f);
    Vec2 target_vel = input.has_move ? vec2_scale(move_dir, target_speed) : Vec2{0.0f, 0.0f};

    if (input.has_move) {
        float accel = p.on_ground ? (cfg.ground_acceleration * terrain.accel_mult) : cfg.air_acceleration;
        p.vel.x = approach(p.vel.x, target_vel.x, accel * fixed_dt);
        p.vel.y = approach(p.vel.y, target_vel.y, accel * fixed_dt);
    } else {
        float decel = p.on_ground ? (cfg.ground_deceleration * terrain.decel_mult) : cfg.air_deceleration;
        p.vel.x = approach(p.vel.x, 0.0f, decel * fixed_dt);
        p.vel.y = approach(p.vel.y, 0.0f, decel * fixed_dt);
    }

    float friction = p.on_ground ? (cfg.ground_friction * terrain.friction_mult) : cfg.air_friction;
    float speed = vec2_length(p.vel);
    if (speed > 1e-5f) {
        float damped = std::max(0.0f, speed - friction * fixed_dt);
        p.vel = vec2_scale(p.vel, damped / speed);
    }

    if (p.on_ground && input.has_move && ground.normal.y < cfg.slope_limit_normal_y) {
        Vec2 downhill = vec2_normalize({-ground.normal.x, -ground.normal.z});
        float slope_factor = clamp01((cfg.slope_limit_normal_y - ground.normal.y) / std::max(0.0001f, cfg.slope_limit_normal_y));
        p.vel = vec2_add(p.vel, vec2_scale(downhill, cfg.slope_slide_accel * terrain.slide_mult * slope_factor * fixed_dt));
        g_physics.sliding = slope_factor > 0.02f;
    }

    float max_hspeed = cfg.max_speed * cfg.run_multiplier * 2.0f;
    float hspeed = vec2_length(p.vel);
    if (hspeed > max_hspeed && hspeed > 1e-5f) {
        p.vel = vec2_scale(p.vel, max_hspeed / hspeed);
    }

    Vec2 horizontal_delta = vec2_scale(p.vel, fixed_dt);
    move_player_horizontal(p, world, cfg, horizontal_delta, move_dir);

    p.pos_y += p.vel_y * fixed_dt;

    GroundProbeResult post_ground = probe_ground(p, world, cfg, false);
    if (post_ground.has_hit) {
        bool landing = (p.vel_y <= 0.0f) && (p.pos_y <= post_ground.height + cfg.ground_tolerance);
        bool snap = (p.vel_y <= 0.0f) && (p.pos_y <= post_ground.height + cfg.ground_snap);
        if (landing || snap) {
            p.pos_y = post_ground.height;
            p.vel_y = 0.0f;
            p.on_ground = true;
            p.ground_height = post_ground.height;
            g_physics.coyote_timer = cfg.coyote_time;
        } else {
            p.on_ground = false;
            p.ground_height = post_ground.height;
        }
        g_physics.ground_normal = post_ground.normal;
        g_physics.terrain = post_ground.terrain;
        terrain = terrain_profile_for(post_ground.terrain, cfg);
        g_physics.terrain_name = terrain.label;
    } else {
        p.on_ground = false;
    }

    if (p.pos_y < 0.0f) {
        p.pos_y = 0.0f;
        p.vel_y = 0.0f;
        p.on_ground = true;
    }

    if (input.has_move) {
        p.target_rotation = std::atan2(move_dir.x, move_dir.y) * (180.0f / kPi);
        if (p.target_rotation < 0.0f) p.target_rotation += 360.0f;
    }

    float rot_diff = p.target_rotation - p.rotation;
    while (rot_diff > 180.0f) rot_diff -= 360.0f;
    while (rot_diff < -180.0f) rot_diff += 360.0f;
    p.rotation += rot_diff * std::min(1.0f, cfg.rotation_smoothing * fixed_dt);
    while (p.rotation >= 360.0f) p.rotation -= 360.0f;
    while (p.rotation < 0.0f) p.rotation += 360.0f;

    if (p.rotation >= 315.0f || p.rotation < 45.0f) p.facing_dir = 0;
    else if (p.rotation < 135.0f) p.facing_dir = 1;
    else if (p.rotation < 225.0f) p.facing_dir = 2;
    else p.facing_dir = 3;

    p.can_jump = !input.jump_held;
}

void reset_player_physics_runtime(bool clear_timers) {
    g_physics.accumulator = 0.0f;
    g_physics.alpha = 0.0f;
    g_physics.prev_pos = g_player.pos;
    g_physics.prev_pos_y = g_player.pos_y;
    g_physics.prev_rotation = g_player.rotation;
    g_physics.render_pos = g_player.pos;
    g_physics.render_pos_y = g_player.pos_y;
    g_physics.render_rotation = g_player.rotation;
    g_physics.ground_normal = {0.0f, 1.0f, 0.0f};
    g_physics.collision_normal = {0.0f, 0.0f};
    g_physics.debug_ray_count = 0;
    g_physics.terrain = TerrainPhysicsType::Normal;
    g_physics.terrain_name = "Normal";
    g_physics.stepped = false;
    g_physics.hit_x = false;
    g_physics.hit_z = false;
    g_physics.sliding = false;
    if (clear_timers) {
        g_physics.jump_buffer_timer = 0.0f;
        g_physics.coyote_timer = 0.0f;
        g_physics.jump_was_held = false;
    }
    g_player.w = g_physics_cfg.collider_width;
    g_player.h = g_physics_cfg.collider_depth;
}

void step_player_physics(const PlayerPhysicsInput& input, float frame_dt) {
    if (!g_world) return;

    float dt = std::clamp(frame_dt, 0.0001f, 0.1f);
    float fixed_dt = g_physics_cfg.fixed_timestep;
    if (fixed_dt <= 0.0f) fixed_dt = 1.0f / 120.0f;

    g_physics.accumulator += dt;
    float max_acc = fixed_dt * (float)std::max(1, g_physics_cfg.max_substeps);
    if (g_physics.accumulator > max_acc) g_physics.accumulator = max_acc;

    int steps = 0;
    while (g_physics.accumulator >= fixed_dt && steps < g_physics_cfg.max_substeps) {
        g_physics.prev_pos = g_player.pos;
        g_physics.prev_pos_y = g_player.pos_y;
        g_physics.prev_rotation = g_player.rotation;

        apply_single_physics_step(input, fixed_dt);

        g_physics.accumulator -= fixed_dt;
        steps++;
    }

    if (steps == 0) {
        g_physics.prev_pos = g_player.pos;
        g_physics.prev_pos_y = g_player.pos_y;
        g_physics.prev_rotation = g_player.rotation;
    }

    g_physics.alpha = clamp01(g_physics.accumulator / fixed_dt);
    g_physics.render_pos = vec2_lerp(g_physics.prev_pos, g_player.pos, g_physics.alpha);
    g_physics.render_pos_y = lerp(g_physics.prev_pos_y, g_player.pos_y, g_physics.alpha);

    float rot_a = g_physics.prev_rotation;
    float rot_b = g_player.rotation;
    float rot_delta = rot_b - rot_a;
    while (rot_delta > 180.0f) rot_delta -= 360.0f;
    while (rot_delta < -180.0f) rot_delta += 360.0f;
    g_physics.render_rotation = rot_a + rot_delta * g_physics.alpha;
    while (g_physics.render_rotation >= 360.0f) g_physics.render_rotation -= 360.0f;
    while (g_physics.render_rotation < 0.0f) g_physics.render_rotation += 360.0f;
}
