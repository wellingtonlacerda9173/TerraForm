#include "save_load.h"

#include "blocks.h"              // Block, kBlockTypeCount, is_ground_like
#include "math_core.h"           // Vec2
#include "world.h"               // World, g_world, surface_height_at
#include "camera.h"              // Camera3D, g_camera, reset_camera_near_player
#include "player_physics.h"      // Player, g_player, reset_player_physics_runtime, world_to_tile is math_core.h
#include "items_particles.h"     // Particle/ItemDrop types, g_particles, g_drops
#include "inventory_crafting.h"  // g_inventory, g_selected
#include "modules_building.h"    // rebuild_modules_from_world
#include "game_state.h"          // UnlockProgress (type)
#include "config_types.h"        // MiniMapRuntime, MapWaypoint (types of g_minimap)

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

// Globais de estado de jogo ainda definidas em main.cpp (dono continua sendo main.cpp -
// nao fazem parte desta etapa de extracao). Todas ja eram nao-static em main.cpp
// (extraidas/expostas em fases anteriores); so precisamos da declaracao aqui tambem,
// mesmo padrao de g_physics_cfg em camera.cpp / g_terrain_cfg em world.cpp.
extern float g_energy;        // Deprecated, use g_base_energy
extern float g_water_res;     // Deprecated, maps to g_player_water
extern float g_oxygen;        // Deprecated, maps to g_player_oxygen
extern float g_food;          // Deprecated, maps to g_player_food
extern float g_day_time;
extern float g_temperature;
extern float g_co2_level;
extern float g_atmosphere;
extern float g_terraform;
extern bool g_victory;
extern bool g_show_build_menu;
extern bool g_surface_dirty;
extern int g_base_x;
extern int g_base_y;
extern TerraPhase g_phase;
extern UnlockProgress g_unlocks;
extern MiniMapRuntime g_minimap;
extern CameraConfig g_camera_cfg;

// g_cam_pos lost its "static" in main.cpp specifically because load_game() below (moved
// to this other translation unit) needs to write it on load - same reasoning as
// g_shooting_stars above. main.cpp keeps the definition (still the "legacy 2D camera
// position" used for smoothing elsewhere in that file, per its own comment there).
extern Vec2 g_cam_pos;

// g_shooting_stars lost its "static" in main.cpp specifically because load_game() below
// (moved to this other translation unit) needs to clear it on load - same pattern as
// g_day_time/g_alerts/g_base_cfg losing "static" for modules_building.cpp's
// update_modules(). Its type (ShootingStar) is defined in items_particles.h, but the
// vector instance itself stays owned/defined by main.cpp (sky/day-night system, a separate
// future extraction stage), so this is a plain extern here, not a header declaration.
extern std::vector<ShootingStar> g_shooting_stars;

// kEnergyMax is a compile-time literal (not mutable state) defined in main.cpp, which
// keeps its own copy too (other deprecated-resource clamps there still use it). Since it's
// a literal, not state, this file keeps its own static constexpr copy rather than sharing
// it via extern - same pattern as kDayLength/kTempThawing in modules_building.cpp/world.cpp.
static constexpr float kEnergyMax = 500.0f;

bool save_game(const char* path) {
    if (!g_world) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;

    const char magic[4] = {'T', 'F', '3', 'D'};  // Atualizado para 3D
    uint32_t version = 6;  // Version 6 - adds fog of war + waypoints
    uint32_t w = (uint32_t)g_world->w;
    uint32_t h = (uint32_t)g_world->h;
    uint32_t seed = (uint32_t)g_world->seed;

    f.write(magic, 4);
    f.write((const char*)&version, sizeof(version));
    f.write((const char*)&w, sizeof(w));
    f.write((const char*)&h, sizeof(h));
    f.write((const char*)&seed, sizeof(seed));

    f.write((const char*)&g_player.pos, sizeof(g_player.pos));
    f.write((const char*)&g_player.vel, sizeof(g_player.vel));
    int32_t hp = (int32_t)g_player.hp;
    f.write((const char*)&hp, sizeof(hp));

    uint8_t sel = (uint8_t)g_selected;
    f.write((const char*)&sel, sizeof(sel));

    uint32_t inv_count = (uint32_t)kBlockTypeCount;
    f.write((const char*)&inv_count, sizeof(inv_count));
    for (uint32_t i = 0; i < inv_count; ++i) {
        int32_t c = (int32_t)g_inventory[(size_t)i];
        f.write((const char*)&c, sizeof(c));
    }

    f.write((const char*)&g_energy, sizeof(g_energy));
    f.write((const char*)&g_water_res, sizeof(g_water_res));
    f.write((const char*)&g_oxygen, sizeof(g_oxygen));
    f.write((const char*)&g_day_time, sizeof(g_day_time));

    // Version 3 additions
    f.write((const char*)&g_base_x, sizeof(g_base_x));
    f.write((const char*)&g_base_y, sizeof(g_base_y));
    f.write((const char*)&g_food, sizeof(g_food));
    f.write((const char*)&g_temperature, sizeof(g_temperature));
    f.write((const char*)&g_co2_level, sizeof(g_co2_level));
    f.write((const char*)&g_atmosphere, sizeof(g_atmosphere));
    f.write((const char*)&g_terraform, sizeof(g_terraform));
    uint8_t phase = (uint8_t)g_phase;
    f.write((const char*)&phase, sizeof(phase));

    // Unlocks
    f.write((const char*)&g_unlocks.total_stone, sizeof(g_unlocks.total_stone));
    f.write((const char*)&g_unlocks.total_iron, sizeof(g_unlocks.total_iron));
    f.write((const char*)&g_unlocks.total_coal, sizeof(g_unlocks.total_coal));
    f.write((const char*)&g_unlocks.total_copper, sizeof(g_unlocks.total_copper));
    f.write((const char*)&g_unlocks.total_wood, sizeof(g_unlocks.total_wood));
    uint8_t unlocks_flags =
        (g_unlocks.solar_unlocked ? 1 : 0) |
        (g_unlocks.water_extractor_unlocked ? 2 : 0) |
        (g_unlocks.o2_generator_unlocked ? 4 : 0) |
        (g_unlocks.greenhouse_unlocked ? 8 : 0) |
        (g_unlocks.co2_factory_unlocked ? 16 : 0) |
        (g_unlocks.habitat_unlocked ? 32 : 0) |
        (g_unlocks.terraformer_unlocked ? 64 : 0);
    f.write((const char*)&unlocks_flags, sizeof(unlocks_flags));

    // Version 4: Camera 3D settings
    f.write((const char*)&g_camera.distance, sizeof(g_camera.distance));
    f.write((const char*)&g_camera.yaw, sizeof(g_camera.yaw));
    f.write((const char*)&g_camera.pitch, sizeof(g_camera.pitch));
    f.write((const char*)&g_camera.sensitivity, sizeof(g_camera.sensitivity));
    f.write((const char*)&g_player.rotation, sizeof(g_player.rotation));

    static_assert(sizeof(Block) == 1, "Block must be 1 byte for save format.");
    static_assert(sizeof(int16_t) == 2, "int16_t must be 2 bytes for save format.");

    // Ground layer (solo), heightmap e depois top layer (objetos/overrides)
    f.write((const char*)g_world->ground.data(), (std::streamsize)g_world->ground.size());
    f.write((const char*)g_world->heightmap.data(), (std::streamsize)(g_world->heightmap.size() * sizeof(int16_t)));
    f.write((const char*)g_world->tiles.data(), (std::streamsize)g_world->tiles.size());

    // Version 6: Fog of war e waypoints
    // Fog of war
    uint32_t explored_size = (uint32_t)g_minimap.explored.size();
    f.write((const char*)&explored_size, sizeof(explored_size));
    if (explored_size > 0) {
        f.write((const char*)g_minimap.explored.data(), (std::streamsize)explored_size);
    }

    // Waypoints
    uint32_t waypoint_count = (uint32_t)g_minimap.waypoints.size();
    f.write((const char*)&waypoint_count, sizeof(waypoint_count));
    for (const auto& wp : g_minimap.waypoints) {
        f.write((const char*)&wp.x, sizeof(wp.x));
        f.write((const char*)&wp.y, sizeof(wp.y));
        f.write((const char*)&wp.r, sizeof(wp.r));
        f.write((const char*)&wp.g, sizeof(wp.g));
        f.write((const char*)&wp.b, sizeof(wp.b));
        uint8_t visible = wp.visible ? 1 : 0;
        f.write((const char*)&visible, sizeof(visible));
        uint32_t label_len = (uint32_t)wp.label.size();
        f.write((const char*)&label_len, sizeof(label_len));
        if (label_len > 0) {
            f.write(wp.label.c_str(), (std::streamsize)label_len);
        }
    }

    return (bool)f;
}

bool load_game(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[4] = {};
    f.read(magic, 4);
    // Aceitar tanto TF2D (versoes antigas) quanto TF3D (versao 4+)
    bool valid_magic = (magic[0] == 'T' && magic[1] == 'F' &&
                        (magic[2] == '2' || magic[2] == '3') && magic[3] == 'D');
    if (!f || !valid_magic) return false;

    uint32_t version = 0;
    uint32_t w = 0, h = 0, seed = 0;
    f.read((char*)&version, sizeof(version));
    f.read((char*)&w, sizeof(w));
    f.read((char*)&h, sizeof(h));
    f.read((char*)&seed, sizeof(seed));
    if (!f || w == 0 || h == 0 || w > 4096 || h > 4096) return false;

    Vec2 pos{}, vel{};
    int32_t hp = 100;
    uint8_t sel = (uint8_t)Block::Dirt;
    f.read((char*)&pos, sizeof(pos));
    f.read((char*)&vel, sizeof(vel));
    f.read((char*)&hp, sizeof(hp));
    f.read((char*)&sel, sizeof(sel));
    if (!f) return false;

    std::array<int, kBlockTypeCount> inv = {};
    inv.fill(0);

    float energy = 0.0f, water_res = 100.0f, oxygen = 100.0f, day_time = 0.0f;
    float food = 100.0f, temperature = -60.0f, co2_level = 0.0f, atmosphere = 5.0f, terraform = 0.0f;
    int base_x = 0, base_y = 0;
    uint8_t phase = 0;
    UnlockProgress unlocks{};
    float placeholder_fuel = 100.0f; // Placeholder para compatibilidade (era jetpack_fuel)

    // Camera 3D settings (Version 4+)
    float cam_distance = g_camera_cfg.spawn_distance, cam_yaw = g_camera_cfg.spawn_yaw, cam_pitch = g_camera_cfg.spawn_pitch, cam_sensitivity = 0.25f;
    float player_rotation = 180.0f;

    if (version == 1) {
        constexpr uint32_t kV1InvCount = (uint32_t)Block::Iron + 1;
        for (uint32_t i = 0; i < kV1InvCount; ++i) {
            int32_t c = 0;
            f.read((char*)&c, sizeof(c));
            if (i < (uint32_t)kBlockTypeCount) inv[(size_t)i] = (int)c;
        }
    } else if (version >= 2 && version <= 5) {
        uint32_t inv_count = 0;
        f.read((char*)&inv_count, sizeof(inv_count));
        if (!f || inv_count > 4096) return false;
        for (uint32_t i = 0; i < inv_count; ++i) {
            int32_t c = 0;
            f.read((char*)&c, sizeof(c));
            if (i < (uint32_t)kBlockTypeCount) inv[(size_t)i] = (int)c;
        }
        f.read((char*)&energy, sizeof(energy));
        f.read((char*)&water_res, sizeof(water_res));
        f.read((char*)&oxygen, sizeof(oxygen));
        f.read((char*)&day_time, sizeof(day_time));

        if (version >= 3) {
            // Version 3 additions
            f.read((char*)&base_x, sizeof(base_x));
            f.read((char*)&base_y, sizeof(base_y));
            f.read((char*)&food, sizeof(food));
            f.read((char*)&temperature, sizeof(temperature));
            f.read((char*)&co2_level, sizeof(co2_level));
            f.read((char*)&atmosphere, sizeof(atmosphere));
            f.read((char*)&terraform, sizeof(terraform));
            f.read((char*)&phase, sizeof(phase));

            // Unlocks
            f.read((char*)&unlocks.total_stone, sizeof(unlocks.total_stone));
            f.read((char*)&unlocks.total_iron, sizeof(unlocks.total_iron));
            f.read((char*)&unlocks.total_coal, sizeof(unlocks.total_coal));
            f.read((char*)&unlocks.total_copper, sizeof(unlocks.total_copper));
            f.read((char*)&unlocks.total_wood, sizeof(unlocks.total_wood));
            uint8_t unlocks_flags = 0;
            f.read((char*)&unlocks_flags, sizeof(unlocks_flags));
            unlocks.solar_unlocked = (unlocks_flags & 1) != 0;
            unlocks.water_extractor_unlocked = (unlocks_flags & 2) != 0;
            unlocks.o2_generator_unlocked = (unlocks_flags & 4) != 0;
            unlocks.greenhouse_unlocked = (unlocks_flags & 8) != 0;
            unlocks.co2_factory_unlocked = (unlocks_flags & 16) != 0;
            unlocks.habitat_unlocked = (unlocks_flags & 32) != 0;
            unlocks.terraformer_unlocked = (unlocks_flags & 64) != 0;

            if (version == 3) {
                f.read((char*)&placeholder_fuel, sizeof(placeholder_fuel)); // Compatibilidade v3
            }
        }

        // Version 4: Camera 3D settings
        if (version >= 4) {
            f.read((char*)&cam_distance, sizeof(cam_distance));
            f.read((char*)&cam_yaw, sizeof(cam_yaw));
            f.read((char*)&cam_pitch, sizeof(cam_pitch));
            f.read((char*)&cam_sensitivity, sizeof(cam_sensitivity));
            f.read((char*)&player_rotation, sizeof(player_rotation));
        }
    } else {
        return false;
    }
    if (!f) return false;

    World* nw = new World((int)w, (int)h, (unsigned)seed);
    size_t tile_count = (size_t)w * (size_t)h;
    nw->tiles.assign(tile_count, Block::Air);
    nw->ground.assign(tile_count, Block::Dirt);
    nw->heightmap.assign(tile_count, 0);

    if (version >= 5) {
        // v5+: ground layer + heightmap + top layer
        std::vector<uint8_t> raw_ground(tile_count, 0);
        std::vector<int16_t> raw_h(tile_count, 0);
        std::vector<uint8_t> raw_tiles(tile_count, 0);

        f.read((char*)raw_ground.data(), (std::streamsize)raw_ground.size());
        f.read((char*)raw_h.data(), (std::streamsize)(raw_h.size() * sizeof(int16_t)));
        f.read((char*)raw_tiles.data(), (std::streamsize)raw_tiles.size());
        if (!f) {
            delete nw;
            return false;
        }

        for (size_t i = 0; i < tile_count; ++i) {
            uint8_t gv = raw_ground[i];
            uint8_t tv = raw_tiles[i];

            nw->ground[i] = (gv < (uint8_t)kBlockTypeCount) ? (Block)gv : Block::Dirt;
            nw->tiles[i] = (tv < (uint8_t)kBlockTypeCount) ? (Block)tv : Block::Air;

            int16_t hh = raw_h[i];
            nw->heightmap[i] = (int16_t)std::clamp((int)hh, 0, 256);
        }
    } else {
        // v1..v4: apenas tiles (mundo era "plano").
        std::vector<uint8_t> raw(tile_count, 0);
        f.read((char*)raw.data(), (std::streamsize)raw.size());
        if (!f) {
            delete nw;
            return false;
        }
        for (size_t i = 0; i < tile_count; ++i) {
            uint8_t v = raw[i];
            nw->tiles[i] = (v < (uint8_t)kBlockTypeCount) ? (Block)v : Block::Air;
            // Melhor esforco: manter solo coerente com tiles walkable; objetos ficam no top layer.
            nw->ground[i] = (nw->tiles[i] != Block::Air && is_ground_like(nw->tiles[i])) ? nw->tiles[i] : Block::Dirt;
            nw->heightmap[i] = 0;
        }
    }
    nw->rebuild_surface_cache();

    delete g_world;
    g_world = nw;
    g_player.pos = pos;
    g_player.vel = vel;
    g_player.vel_y = 0.0f;
    {
        int tx = world_to_tile(g_player.pos.x);
        int tz = world_to_tile(g_player.pos.y);
        if (g_world->in_bounds(tx, tz)) {
            g_player.pos_y = surface_height_at(*g_world, tx, tz);
        } else {
            g_player.pos_y = 0.0f;
        }
        g_player.ground_height = g_player.pos_y;
        g_player.on_ground = true;
        g_player.can_jump = true;
    }
    g_player.hp = std::clamp((int)hp, 0, 100);
    g_selected = ((int)sel >= 0 && (int)sel < kBlockTypeCount) ? (Block)sel : Block::Dirt;
    g_inventory = inv;
    g_particles.clear();
    g_shooting_stars.clear();
    g_drops.clear();

    g_energy = std::clamp(energy, 0.0f, kEnergyMax);
    g_water_res = std::clamp(water_res, 0.0f, 100.0f);
    g_oxygen = std::clamp(oxygen, 0.0f, 100.0f);
    g_day_time = std::fmax(0.0f, day_time);

    // Version 3 data
    g_base_x = base_x;
    g_base_y = base_y;
    g_food = std::clamp(food, 0.0f, 100.0f);
    g_temperature = std::clamp(temperature, -100.0f, 100.0f);
    g_co2_level = std::clamp(co2_level, 0.0f, 100.0f);
    g_atmosphere = std::clamp(atmosphere, 0.0f, 100.0f);
    g_terraform = std::clamp(terraform, 0.0f, 100.0f);
    g_phase = (phase < 5) ? (TerraPhase)phase : TerraPhase::Frozen;
    g_unlocks = unlocks;

    // Camera 3D settings (Version 4)
    g_camera.distance = std::clamp(cam_distance, g_camera.min_distance, g_camera.max_distance);
    g_camera.effective_distance = g_camera.distance;
    g_camera.yaw = cam_yaw;
    g_camera.pitch = std::clamp(cam_pitch, g_camera.min_pitch, g_camera.max_pitch);
    g_camera.sensitivity = std::clamp(cam_sensitivity, 0.05f, 1.0f);
    g_player.rotation = player_rotation;
    g_player.target_rotation = player_rotation;
    reset_camera_near_player(false);

    g_cam_pos = g_player.pos;
    reset_player_physics_runtime(true);
    g_surface_dirty = true;
    g_victory = false;
    g_show_build_menu = false;
    rebuild_modules_from_world();

    // Version 6: Carregar fog of war e waypoints
    if (version >= 6) {
        // Fog of war
        uint32_t explored_size = 0;
        f.read((char*)&explored_size, sizeof(explored_size));
        if (f && explored_size > 0 && explored_size == (uint32_t)(g_world->w * g_world->h)) {
            g_minimap.explored.resize(explored_size);
            f.read((char*)g_minimap.explored.data(), (std::streamsize)explored_size);
        } else {
            // Inicializar fog of war se nao carregou corretamente
            g_minimap.explored.clear();
            g_minimap.explored.resize((size_t)g_world->w * (size_t)g_world->h, 0);
        }

        // Waypoints
        uint32_t waypoint_count = 0;
        f.read((char*)&waypoint_count, sizeof(waypoint_count));
        g_minimap.waypoints.clear();
        if (f && waypoint_count > 0 && waypoint_count <= 100) {
            for (uint32_t i = 0; i < waypoint_count; ++i) {
                MapWaypoint wp;
                f.read((char*)&wp.x, sizeof(wp.x));
                f.read((char*)&wp.y, sizeof(wp.y));
                f.read((char*)&wp.r, sizeof(wp.r));
                f.read((char*)&wp.g, sizeof(wp.g));
                f.read((char*)&wp.b, sizeof(wp.b));
                uint8_t visible = 0;
                f.read((char*)&visible, sizeof(visible));
                wp.visible = (visible != 0);
                uint32_t label_len = 0;
                f.read((char*)&label_len, sizeof(label_len));
                if (label_len > 0 && label_len < 256) {
                    wp.label.resize(label_len);
                    f.read(&wp.label[0], (std::streamsize)label_len);
                }
                g_minimap.waypoints.push_back(wp);
            }
        }
    } else {
        // Versao antiga - inicializar fog of war e waypoints
        g_minimap.explored.clear();
        g_minimap.explored.resize((size_t)g_world->w * (size_t)g_world->h, 0);
        g_minimap.waypoints.clear();

        // Revelar area ao redor da base
        int reveal_radius = 20;
        for (int dy = -reveal_radius; dy <= reveal_radius; ++dy) {
            for (int dx = -reveal_radius; dx <= reveal_radius; ++dx) {
                int x = g_base_x + dx;
                int y = g_base_y + dy;
                if (g_world->in_bounds(x, y)) {
                    float dist = std::sqrt((float)(dx * dx + dy * dy));
                    if (dist <= (float)reveal_radius) {
                        g_minimap.explored[(size_t)y * (size_t)g_world->w + (size_t)x] = 255;
                    }
                }
            }
        }
    }

    g_minimap.dirty_full = true;
    g_minimap.world_map_open = false;

    return true;
}
