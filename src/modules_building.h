#pragma once

#include "blocks.h"

#include <string>
#include <vector>

// ============= Base Building / Modules =============
// Extracted verbatim from main.cpp (original lines ~54-65, 159-166, 332-350, 416-662,
// 1742-1754, 1785-1854, 1920-2066, 2606-3029): the construction-queue/build-slot/module
// types, generate_base() (landing-site generation), rebuild_modules_from_world() (save-
// load helper), the module stats/status/unlock-requirement system, and update_modules()
// (the per-frame base simulation). save_game()/load_game() sit in the middle of this
// original range in main.cpp but are NOT part of this extraction - they stay in main.cpp
// (a separate future save_load.h/.cpp stage), and just call rebuild_modules_from_world()
// declared below.
//
// World is only ever used here by reference/pointer, so this header only needs a forward
// declaration - same "owner keeps the definition, users just forward-declare" rule used
// for the world/camera/player_physics split.
struct World;

// ---- Construction queue ----
struct ConstructionJob {
    Block module_type = Block::Air;
    int slot_index = -1;
    float time_remaining = 0.0f;
    float total_time = 0.0f;
    bool active = false;
};

// O unico vetor de fila de construcao do jogo. Definido (nao-static) em
// modules_building.cpp; main.cpp continua usando g_construction_queue diretamente
// (render/input do menu de build, raycast de colocacao, build_physics_test_map) atraves
// desta declaracao extern - mesmo padrao de g_world/g_camera nos estagios anteriores.
// player_physics.cpp's spawn_player_new_game() also clears it directly now that
// ConstructionJob is a complete type there (via this header) - see the note removed from
// player_physics.cpp's clear_construction_queue() wrapper, which is gone now that this
// header exists.
extern std::vector<ConstructionJob> g_construction_queue;

// ---- Build slots for the base ----
struct BuildSlotInfo {
    int x = 0;
    int y = 0;
    Block assigned_module = Block::Air;  // Air means empty slot
    std::string label;
};

// Idem g_construction_queue acima: main.cpp continua lendo/escrevendo g_build_slots
// diretamente (render/input do menu de build, build_physics_test_map).
extern std::vector<BuildSlotInfo> g_build_slots;

// ---- Modules ----
// Module status enum (precisa vir antes de struct Module)
enum class ModuleStatus {
    Available,      // Can be built
    Blocked,        // Missing resources
    Building,       // Under construction
    Active,         // Running normally
    NoPower,        // Needs energy
    Damaged         // Needs repair
};

struct Module {
    int x = 0;
    int y = 0;
    Block type = Block::SolarPanel;
    float t = 0.0f;
    float health = 100.0f;     // 0-100, se <= 0 fica Damaged
    ModuleStatus status = ModuleStatus::Active;
};

// Idem g_construction_queue acima: main.cpp continua lendo/escrevendo g_modules
// diretamente (render do mundo/minimapa, HUD, raycast de colocacao/remocao,
// build_physics_test_map).
extern std::vector<Module> g_modules;

// Module production/consumption rates (per minute)
struct ModuleStats {
    const char* name;
    const char* description;
    float energy_production = 0.0f;   // +Energy/min
    float energy_consumption = 0.0f;  // -Energy/min
    float oxygen_production = 0.0f;   // +O2/min
    float water_production = 0.0f;    // +Water/min
    float food_production = 0.0f;     // +Food/min
    float integrity_bonus = 0.0f;     // Repair rate/min
    float co2_production = 0.0f;      // For terraforming
    float construction_time = 30.0f;  // Seconds to build
};

// Get module statistics. Lost "static": main.cpp's build-menu rendering (outside this
// stage's scope) also calls it.
ModuleStats get_module_stats(Block b);

// Start construction of a module (spends CraftCost via inventory_crafting.h, queues a
// ConstructionJob). Lost "static": main.cpp's build-menu input (outside this stage's
// scope) also calls it.
bool start_construction(Block module_type, int slot_index);

// NOTE: get_module_status(Block)/status_string(ModuleStatus) are NOT declared here on
// purpose - grep across the whole file shows neither is ever called from outside this
// module (both were already dead code before this refactor: defined but unused).  Since
// nothing outside modules_building.cpp touches them, they stay `static` (internal)
// there instead of being exposed here.

// ---- Unlock system ----
// UnlockRequirement/get_unlock_requirement are NOT declared here on purpose either - they
// are only ever used internally by is_unlocked/check_unlocks/unlock_progress_string
// below (all in this same file), so they stay file-local (static) to
// modules_building.cpp.
bool is_unlocked(Block b);
void check_unlocks();
std::string unlock_progress_string(Block b);

// ---- Base generation / simulation ----
void generate_base(World& world);
void rebuild_modules_from_world();
void update_modules(World& world, float dt);
