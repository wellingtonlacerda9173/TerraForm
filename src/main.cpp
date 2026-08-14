#include "platform.h"
#include "math_core.h"
#include "noise.h"
#include "blocks.h"
#include "textures.h"
#include "config_types.h"
#include "world.h"
#include "camera.h"
#include "config_io.h"
#include "game_state.h"
#include "player_physics.h"
#include "items_particles.h"
#include "inventory_crafting.h"
#include "modules_building.h"
#include "font.h"                // init_font/draw_text/estimate_text_w_px (font extraction stage)
#include "save_load.h"           // save_game/load_game (save_load extraction stage)
#include "minimap.h"             // fog of war, waypoints, minimap/world map render (minimap extraction stage)
#include "render_primitives.h"   // render_quad/render_cube_3d/render_wall_3d_tex/etc. (render_primitives extraction stage)
#include "render_player.h"       // render_player_topdown/render_astronaut (render_player extraction stage)
#include "lighting.h"            // Light2D/g_lighting/compute_lightmap/sample_lightmap/etc. (lighting extraction stage)
#include "sky.h"                 // SkyPalette/compute_sky_palette/render_alien_sky/update_shooting_stars (sky extraction stage)

// ===========================
// TerraFormer 2D (prototype)
// Win32 + OpenGL (immediate mode)
// ===========================

// ============= SISTEMA DE CORES CENTRALIZADO (UX) =============
// Cores funcionais - cada cor tem significado consistente
static const float kColorHp[]       = {0.90f, 0.14f, 0.18f, 1.0f};   // Vermelho - vida/dano
static const float kColorOxygen[]   = {0.20f, 0.85f, 0.55f, 1.0f};   // Verde - oxigenio
static const float kColorWater[]    = {0.25f, 0.65f, 0.95f, 1.0f};   // Azul - agua
static const float kColorEnergy[]   = {0.95f, 0.84f, 0.25f, 1.0f};   // Amarelo - energia
static const float kColorFood[]     = {0.85f, 0.65f, 0.25f, 1.0f};   // Laranja - comida
static const float kColorDanger[]   = {0.95f, 0.35f, 0.20f, 1.0f};   // Vermelho-laranja - perigo
static const float kColorSuccess[]  = {0.30f, 0.95f, 0.45f, 1.0f};   // Verde brilhante - sucesso
static const float kColorLocked[]   = {0.50f, 0.50f, 0.55f, 1.0f};   // Cinza - bloqueado
static const float kColorWarning[]  = {0.95f, 0.75f, 0.20f, 1.0f};   // Amarelo-laranja - aviso

// Cores de UI - paineis e textos
static const float kColorPanelBg[]      = {0.08f, 0.08f, 0.10f, 0.85f};  // Fundo de painel
static const float kColorPanelBorder[]  = {0.30f, 0.55f, 0.85f, 0.90f};  // Borda azul
static const float kColorTextPrimary[]  = {0.95f, 0.95f, 0.95f, 1.0f};   // Texto principal
static const float kColorTextSecondary[]= {0.70f, 0.70f, 0.75f, 0.90f};  // Texto secundario
static const float kColorHighlight[]    = {0.95f, 0.95f, 0.35f, 0.90f};  // Destaque amarelo
static const float kColorSelection[]    = {0.35f, 0.65f, 0.95f, 0.80f};  // Selecao azul

// ============= Resources & Global State =============
// BASE resources (stored in the base, modules fill these). Lost "static": respawn_player_at_base()/
// spawn_player_new_game() (extracted to player_physics.cpp) need external linkage to read/write
// some of these - same pattern as g_oxygen/g_water_res/etc. in textures.cpp.
float g_base_energy = 50.0f;   // 0..500 (energy stored in base)
float g_base_water = 50.0f;    // 0..200 (water stored in base)
float g_base_oxygen = 50.0f;   // 0..200 (oxygen stored in base)
float g_base_food = 50.0f;     // 0..200 (food stored in base)
float g_base_integrity = 100.0f;  // 0..100 (base structural integrity)

static constexpr float kBaseEnergyMax = 500.0f;
static constexpr float kBaseWaterMax = 200.0f;
static constexpr float kBaseOxygenMax = 200.0f;
static constexpr float kBaseFoodMax = 200.0f;
static constexpr float kBaseIntegrityMax_Global = 100.0f;  // For reference before full declaration

// ConstructionJob struct + g_construction_queue moved to modules_building.h/.cpp
// (verbatim) - this is the items_particles/modules_building/inventory_crafting
// extraction stage. modules_building.h (included above) supplies the "extern
// std::vector<ConstructionJob> g_construction_queue;" declaration this file relies on
// (build-menu render/input, build_physics_test_map). The clear_construction_queue()
// wrapper that used to live here is gone: it existed only because player_physics.cpp had
// just a forward declaration of ConstructionJob (an incomplete type, for which
// std::vector<T>::clear() is not guaranteed to work per the standard); now that
// player_physics.cpp includes modules_building.h directly (see its own comment), it
// calls g_construction_queue.clear() itself.

// Alert struct moved to game_state.h (verbatim); g_alerts (the vector) stays here since
// it's used throughout this file's alert system, not just by the feedback subsystem
// extracted into game_state.cpp. Lost "static": spawn_player_new_game() (extracted to
// player_physics.cpp) also clears it.
std::vector<Alert> g_alerts;

// PLAYER resources (suit tanks, refilled at base). Lost "static": respawn_player_at_base()/
// spawn_player_new_game() (extracted to player_physics.cpp) read/write these.
float g_player_oxygen = 100.0f;  // 0..100 (suit O2 tank)
float g_player_water = 100.0f;   // 0..100 (suit water tank)
float g_player_food = 100.0f;    // 0..100 (carried food)

// Legacy compatibility (these map to player resources now). g_energy/g_food lost
// "static": spawn_player_new_game() (extracted to player_physics.cpp) writes them too.
float g_energy = 0.0f;        // Deprecated, use g_base_energy
float g_water_res = 0.0f;     // Deprecated, maps to g_player_water
float g_oxygen = 0.0f;        // Deprecated, maps to g_player_oxygen
float g_food = 100.0f;        // Deprecated, maps to g_player_food

// g_terraform/g_victory/g_co2_level/g_phase lost "static" here: world.cpp's
// recompute_terraform_score/update_phase/terraform_step/melt_ice_around (extracted from
// this file) need external linkage to read/write them from another translation unit -
// same pattern as g_oxygen/g_water_res/etc. in textures.cpp.
float g_terraform = 0.0f;     // 0..100 (computed)
bool g_victory = false;

// Atmosphere & Temperature (Realistic Terraforming)
float g_temperature = -60.0f;  // Starting temp in Celsius (Mars-like)
float g_co2_level = 0.0f;      // 0..100 (atmospheric CO2)
float g_atmosphere = 0.0f;     // 0..100 (atmosphere density)
TerraPhase g_phase = TerraPhase::Frozen;

static constexpr float kEnergyMax = 500.0f;
static constexpr float kTempFrozen = -20.0f;    // Below this: frozen
static constexpr float kTempThawing = 0.0f;     // Water can be liquid
// kTempThawing used to be needed here too (update_modules()'s global ice-melt timer used
// to compare against it directly), but update_modules() has since moved to
// modules_building.cpp (this stage), which keeps its own file-local copy of this same
// literal value instead (see the comment there) - same pattern already used by
// world.cpp (update_phase()/melt_ice_around()) below. kTempThawing itself is left here
// unused rather than removed, to keep this stage's diff minimal and low-risk.
// kTempHabitable/kTempTarget moved to world.cpp only (defined there, static to that TU):
// they were exclusively used by update_phase(), which moved there too.

// Unlock System - tracks total resources ever collected
// UnlockProgress struct moved to game_state.h (verbatim); g_unlocks (the instance) stays
// here since it's used throughout this file's build/unlock logic, not just by the
// feedback subsystem extracted into game_state.cpp. Lost "static": spawn_player_new_game()
// (extracted to player_physics.cpp) resets it on new game.
UnlockProgress g_unlocks;

// ============= SISTEMA DE ONBOARDING =============
// OnboardingState struct + g_onboarding instance moved to game_state.h/game_state.cpp
// (this is the game state / feedback / onboarding extraction stage).

// ============= CONFIGURACOES DE ACESSIBILIDADE =============
// GameSettings struct moved to game_state.h (verbatim); g_settings (the instance) stays
// here since it's used throughout this file's settings-menu logic, not just by the
// feedback subsystem extracted into game_state.cpp.
static GameSettings g_settings;

// ============= FEEDBACK VISUAL =============
// g_screen_flash_red/green, g_hotbar_bounce(+_slot), the CollectPopup struct +
// g_collect_popups, and the unlock-popup globals all moved to
// game_state.h/game_state.cpp (this is the game state / feedback / onboarding
// extraction stage).

// Base location (landing site). g_base_x/g_base_y/g_show_build_menu/g_build_menu_selection
// lost "static": spawn_player_at_base()/spawn_player_new_game() (extracted to
// player_physics.cpp) read/write them - same pattern as g_oxygen/g_water_res/etc. in
// textures.cpp. Still owned by main.cpp: even though generate_base()/update_modules()
// (modules_building.cpp, this stage) and rebuild_modules_from_world() now read/write
// g_base_x/g_base_y too (via their own extern declarations), this file's build-menu
// render/input and HUD code remain their heaviest users, so they stay here for now.
int g_base_x = 0;
int g_base_y = 0;
bool g_show_build_menu = false;
int g_build_menu_selection = 0;
static int g_settings_selection = 0;  // 0=sensibilidade, 1=inverter Y, 2=brilho, 3=escala UI, 4=iluminacao, 5=sombras, 6=bloom, 7=vinheta, 8=voltar
static int g_pause_selection = -1;     // -1=nenhum, 0=continuar, 1=salvar, 2=carregar, 3=config, 4=novo jogo
static int g_menu_selection = -1;      // -1=nenhum, 0=novo jogo, 1=carregar, 2=sair

// Posicao do mouse na tela
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static bool g_mouse_left_clicked = false;  // Flag para clique esquerdo (single frame)

// BuildSlotInfo struct + g_build_slots moved to modules_building.h/.cpp (verbatim) - this
// is the items_particles/modules_building/inventory_crafting extraction stage.
// modules_building.h (included above) supplies the "extern std::vector<BuildSlotInfo>
// g_build_slots;" declaration this file relies on (build-menu render/input,
// generate_base(), build_physics_test_map).

// g_terrain_cfg/g_sky_cfg/g_mining_cfg/g_player_visual_cfg (and their *_config_path
// siblings below) lost "static" here: config_io.cpp's reload_terrain_config/
// reload_sky_config/reload_mining_config/reload_player_visual_config (extracted from
// this file) need external linkage to read/write them from another translation unit —
// same pattern as g_oxygen/g_water_res/etc. in textures.cpp. g_camera_cfg also lost
// "static" (it used to stay static here because only reload_camera_config, still defined
// in this file, touched it) now that camera.cpp reads it too (collision probing, mode
// tuning, etc.) via its own extern declaration. g_camera_config_path stays static: only
// reload_camera_config (still in this file) touches it.
TerrainConfig g_terrain_cfg = {};
SkyConfig g_sky_cfg = {};
CameraConfig g_camera_cfg = {};
MiningConfig g_mining_cfg = {};
PlayerVisualConfig g_player_visual_cfg = {};
// g_base_cfg also lost "static" here (same reasoning as g_terrain_cfg etc. above):
// modules_building.cpp's update_modules() (extracted from this file) now reads it
// (safe_radius, recharge_*_rate, repair_player_hp_per_sec, jetpack_refuel_per_sec) from
// another translation unit. Note this is only the instance losing "static" - the
// BaseConfig struct definition itself (config_types.h) is untouched by this stage.
BaseConfig g_base_cfg = {};
// g_map_cfg lost "static" here: minimap.cpp's render_minimap/render_world_map/add_waypoint/
// remove_nearest_waypoint (extracted from this file) need external linkage to read it from
// another translation unit - same pattern as g_terrain_cfg etc. above.
MapConfig g_map_cfg = {};
// g_minimap lost "static": spawn_player_new_game() (extracted to player_physics.cpp)
// resets its fog-of-war/waypoints on new game.
MiniMapRuntime g_minimap = {};
std::string g_terrain_config_path = "terrain_config.json";
std::string g_sky_config_path = "sky_config.json";
static std::string g_camera_config_path = "camera_config.json";
std::string g_mining_config_path = "mining_config.json";
std::string g_player_visual_config_path = "player_visual.json";
static std::string g_base_config_path = "base_config.json";
static std::string g_map_config_path = "map_config.json";

// struct World moved to world.h (see there for the type + its inline accessors); the
// out-of-line World::gen() terrain generator, and the terraforming free functions that
// used to sit near it in this file, moved to world.cpp.

// Forward declarations (gameplay/render below)
// save_game/load_game forward declarations removed: they moved to save_load.h (real,
// non-static declarations there now) as part of the save_load extraction stage.
// rebuild_modules_from_world/generate_base forward declarations removed: they moved to
// modules_building.h (real, non-static declarations there now) as part of the
// items_particles/modules_building/inventory_crafting extraction stage.
// surface_block_at/object_block_at/surface_height_at/get_block_height forward
// declarations removed: they moved to world.h (real, non-static declarations there now)
// as part of the world/terrain extraction.
// render_quad forward declaration removed: it now comes from render_primitives.h (included
// at the top of this file), whose real (non-forward) declaration supplies it - render_quad's
// definition moved out to render_primitives.cpp as part of the render_primitives/render_player
// extraction stage.
// draw_text forward declaration removed: it now comes from font.h (included at the top of
// this file), which is already visible here.
// set_toast forward declaration removed: it now comes from game_state.h (included at
// the top of this file), which is already visible here.
// reset_player_physics_runtime/step_player_physics forward declarations removed: they
// now come from player_physics.h (included at the top of this file), which also
// supplies the real (non-forward) PlayerPhysicsInput definition.
static void build_physics_test_map(World& world);

// ============= Gameplay State =============
static bool g_quit = false;
static const int WORLD_WIDTH = 512;
static const int WORLD_HEIGHT = 256;
static constexpr float TILE_PX = 16.0f;

// Sistema de zoom para melhor visibilidade
static float g_zoom = 2.0f;  // Zoom padrao 2x (tiles aparecem 32px)
static constexpr float kMinZoom = 1.5f;
static constexpr float kMaxZoom = 4.0f;

// g_world's definition moved to world.cpp (natural owner of the World type it points to);
// world.h supplies the "extern World* g_world;" declaration this file relies on.
// g_cam_pos lost "static" here: save_load.cpp's load_game() (extracted from this file)
// needs external linkage to write it on load - same pattern as g_shooting_stars below.
// This file keeps the definition (legacy 2D camera position, still used for smoothing
// elsewhere in this file).
Vec2 g_cam_pos = {0.0f, 0.0f};  // Mantido para compatibilidade temporaria

// Camera3D/CameraMode/CameraDebugRay and the camera update/collision/visibility functions
// (reset_camera_near_player, update_camera_position, apply_look_at, apply_perspective,
// get_mouse_ray_direction, check_camera_collision, update_camera_for_frame, etc.) moved to
// camera.h/camera.cpp. g_camera and its adaptive-mode state (g_camera_mode, ...) moved with
// them; camera.h supplies the "extern Camera3D g_camera;" (and friends) this file relies on.
bool g_debug = false;  // General debug toggle (not camera-specific); lost "static" here
                       // because camera.cpp's update_camera_for_frame() also reads it.

// struct Player + g_player, TerrainPhysicsType/PhysicsRayDebug/PhysicsRuntime/
// PlayerPhysicsInput + g_physics, and get_player_render_pos()/get_player_render_y()
// moved to player_physics.h/.cpp (this is the player/physics extraction stage).
// player_physics.h (included at the top of this file) supplies the "extern Player
// g_player;" / "extern PhysicsRuntime g_physics;" declarations this file relies on.

// g_physics_cfg/g_physics_config_path lost "static": config_io.cpp's
// reload_physics_config needs external linkage to read/write them (same pattern as
// g_terrain_cfg above / g_oxygen etc. in textures.cpp).
PhysicsConfig g_physics_cfg = {};
std::string g_physics_config_path = "physics_config.json";

static float get_player_render_rotation() { return g_physics.render_rotation; }

// update_camera_for_frame() moved to camera.cpp (camera.h supplies its declaration).

// g_inventory/g_selected moved to inventory_crafting.h/.cpp (this stage) - their natural
// owner among the modules extracted so far. inventory_crafting.h (included at the top of
// this file) supplies the "extern std::array<int, kBlockTypeCount> g_inventory;"/"extern
// Block g_selected;" declarations this file relies on (mining, HUD, hotbar, save/load,
// build menu); player_physics.cpp's spawn_player_new_game() (which grants the starter kit
// and resets the selected block) now gets them from that header too, instead of its own
// local extern declarations.

static bool g_prev_lmb = false;
static bool g_prev_rmb = false;
static bool g_prev_esc = false;
static bool g_prev_enter = false;
static bool g_prev_e = false;  // Tecla de interacao (top-down)
static bool g_prev_f5 = false;
static bool g_prev_f9 = false;
static bool g_prev_l = false;
static bool g_prev_q = false;
static bool g_prev_f3 = false;
static bool g_prev_f6 = false;
static bool g_prev_f7 = false;
static bool g_prev_h = false;
static bool g_prev_tab = false;
static bool g_prev_b = false;
static bool g_prev_m = false;
static bool g_prev_r = false;
static bool g_prev_c = false;

static float g_place_cd = 0.0f;
static float g_drown_accum = 0.0f;

// Mining progress (estilo Minicraft/Minecraft: segurar para quebrar)
static int g_mine_block_x = -1;
static int g_mine_block_y = -1;
static float g_mine_progress = 0.0f; // 0..1
static int g_mine_hits = 0;
static float g_mine_hit_timer = 0.0f;

static bool g_has_target = false;
static int g_target_x = 0;
static int g_target_y = 0;
static bool g_target_in_range = false;

// Target de colocacao (tile onde o RMB vai tentar colocar)
static bool g_has_place_target = false;
static int g_place_x = 0;
static int g_place_y = 0;
static bool g_place_in_range = false;

// Particle/ItemDrop structs + g_particles/g_drops/g_target_drop moved to
// items_particles.h/.cpp (verbatim) - this is the items_particles/modules_building/
// inventory_crafting extraction stage. items_particles.h (included above) supplies the
// extern declarations this file relies on (particle/drop rendering, raycast mining/
// placement, clear() on respawn/new-game/load_game).
//
// ShootingStar's struct definition moved to items_particles.h too (it was textually
// interleaved with Particle/ItemDrop here). update_shooting_stars() has since moved out too
// (the sky extraction stage, declared in sky.h now) but g_shooting_stars (the vector) stays
// right here in main.cpp: it is read/written directly by save_load.cpp's load_game() (clear
// on load) and by this file's own new-game/respawn reset code, not just by the sky system.
// g_shooting_stars lost "static" here: save_load.cpp's load_game() (extracted from this
// file) needs external linkage to clear it on load - same pattern as g_day_time/g_alerts/
// g_base_cfg losing "static" for modules_building.cpp's update_modules().
// Eventos do ceu: estrelas cadentes (camera-relative para parecer "longe" do mundo).
std::vector<ShootingStar> g_shooting_stars;

// ModuleStatus enum + Module struct + g_modules moved to modules_building.h/.cpp
// (verbatim) - same stage as above. modules_building.h (included above) supplies the
// extern declarations this file relies on (world/minimap render, HUD, raycast placement/
// removal, build_physics_test_map).

// Light2D struct, the lightmap/bloom pixel buffers, LightingSettings + its g_lighting
// instance, and the debug toggles (g_debug_lightmap/g_debug_lights) moved to
// lighting.h/.cpp (verbatim) - the lighting extraction stage. lighting.h (included at the
// top of this file) supplies the declarations render_world()/update_game() below still
// rely on directly (per-tile lighting/vignette debug overlays, F3 debug cycle, settings-menu
// lighting options). g_debug_bloom did NOT move with them: grep confirms it was already
// dead code (declared, never read anywhere) before this stage, so it now lives as a
// file-local static inside lighting.cpp instead.

// ============= Generate Base (Landing Site) =============
// generate_base() moved to modules_building.h/.cpp (verbatim) - this is the
// items_particles/modules_building/inventory_crafting extraction stage.
// modules_building.h (included at the top of this file) supplies its
// declaration; player_physics.cpp's spawn_player_new_game() (which calls it to
// set up the landing site on a new game) now gets it from that header too,
// instead of its own local forward declaration.

// GameState enum + g_state, the toast/screen-flash/collect-popup/unlock-popup globals,
// the feedback functions (set_toast/show_error/show_success/add_collect_popup/
// show_unlock_popup/bounce_hotbar_slot), the onboarding functions (show_tip/
// update_onboarding), and the small xorshift RNG (g_rng/rng_next_u32/rng_next_f01) all
// moved to game_state.h/game_state.cpp (game state / feedback / onboarding extraction
// stage).

// g_day_time lost "static" here: modules_building.cpp's update_modules() (extracted from
// this file) now reads/writes it from another translation unit - same pattern as
// g_oxygen/g_water_res/etc. in textures.cpp. kDayLength stays static constexpr here (this
// file's render/sky code still uses it directly); modules_building.cpp keeps its own copy
// of the same literal value rather than sharing it via extern, since it's compile-time
// state, not mutable - same pattern as kTempThawing in world.cpp.
float g_day_time = 0.0f;
static constexpr float kDayLength = 150.0f; // seconds

static float g_stats_timer = 0.0f;
// g_surface_dirty lost "static" here: world.cpp's terraform_step/melt_ice_around
// (extracted from this file) need external linkage to write it from another translation
// unit - same pattern as g_oxygen/g_water_res/etc. in textures.cpp.
bool g_surface_dirty = true;

// ============= Font =============
// init_font()/draw_text()/estimate_text_w_px() (and the file-local g_font_base) moved to
// font.h/font.cpp (verbatim) - the font extraction stage. font.h (included at the top of
// this file) supplies the declarations this file relies on.

// ============= Save/Load =============
static const char* kSavePath = "save_slot0.tf2d";

// rebuild_modules_from_world() moved to modules_building.h/.cpp (verbatim) - same stage
// as generate_base() above. modules_building.h (included at the top of this file)
// supplies its declaration. save_game()/load_game() themselves moved to save_load.h/.cpp
// (verbatim) - the save_load extraction stage; save_load.h (included at the top of this
// file) supplies their declarations.

// approach()/place_player_near()/find_spawn_x()/spawn_player_at_base()/
// respawn_player_at_base()/spawn_player_new_game() moved to player_physics.h/.cpp (this
// is the player/physics extraction stage). player_physics.h supplies the declarations
// this file relies on (place_player_near/find_spawn_x are unused outside that module,
// so they are not declared here).

// update_fog_of_war()/add_waypoint()/remove_nearest_waypoint()/clear_all_waypoints()/
// render_minimap()/render_world_map() (and the file-local get_minimap_color() helper)
// moved to minimap.h/minimap.cpp (verbatim) - the minimap extraction stage. minimap.h
// (included at the top of this file) supplies the declarations this file relies on.

static void build_physics_test_map(World& world) {
    const int cz = world.h / 2;
    const int x0 = 24;
    const int x1 = std::min(world.w - 24, x0 + 380);
    const int z0 = std::max(4, cz - 40);
    const int z1 = std::min(world.h - 5, cz + 40);
    const int16_t base_h = 24;

    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            world.set(x, z, Block::Air);
            world.set_ground(x, z, Block::Stone);
            world.set_height(x, z, base_h);
        }
    }

    // Lanes de material: gelo, areia, pedra e lama.
    for (int x = x0; x <= x1; ++x) {
        for (int z = cz - 34; z <= cz - 26; ++z) world.set_ground(x, z, Block::Ice);
        for (int z = cz - 20; z <= cz - 12; ++z) world.set_ground(x, z, Block::Sand);
        for (int z = cz - 6; z <= cz + 2; ++z) world.set_ground(x, z, Block::Stone);
        for (int z = cz + 8; z <= cz + 16; ++z) world.set_ground(x, z, Block::Organic);
    }

    // Buracos e gaps.
    for (int x = 72; x <= 94; ++x) {
        for (int z = cz - 2; z <= cz + 2; ++z) world.set_height(x, z, 8);
    }
    for (int x = 146; x <= 157; ++x) {
        for (int z = cz + 10; z <= cz + 16; ++z) world.set_height(x, z, 4);
    }

    // Escadas.
    for (int i = 0; i < 10; ++i) {
        int sx = 110 + i * 2;
        int16_t h = (int16_t)(base_h + i * 2);
        for (int x = sx; x < sx + 2; ++x) {
            for (int z = cz + 20; z <= cz + 26; ++z) world.set_height(x, z, h);
        }
    }

    // Rampa longa.
    for (int x = 190; x <= 256; ++x) {
        int16_t h = (int16_t)(base_h + (x - 190) / 3);
        for (int z = cz + 22; z <= cz + 34; ++z) world.set_height(x, z, h);
    }

    // Plataformas altas.
    for (int x = 300; x <= 332; ++x) {
        for (int z = cz - 14; z <= cz - 2; ++z) world.set_height(x, z, base_h + 16);
    }
    for (int x = 334; x <= 366; ++x) {
        for (int z = cz - 14; z <= cz - 2; ++z) world.set_height(x, z, base_h + 24);
    }

    // Obstaculos para testar colisao/step.
    for (int x = 214; x <= 224; x += 2) world.set(x, cz - 1, Block::Stone);
    for (int x = 238; x <= 248; x += 2) world.set(x, cz - 1, Block::Iron);
    world.set(272, cz + 12, Block::Copper);
    world.set(274, cz + 12, Block::Coal);
    world.set(276, cz + 12, Block::Crystal);

    // Degraus baixos de 1 tile para step-climb.
    for (int i = 0; i < 8; ++i) {
        int x = 40 + i * 6;
        int16_t h = (int16_t)(base_h + ((i & 1) ? 2 : 1));
        for (int z = cz - 10; z <= cz - 6; ++z) world.set_height(x, z, h);
    }

    world.rebuild_surface_cache();
    g_surface_dirty = true;
    g_modules.clear();
    g_construction_queue.clear();
    g_alerts.clear();
    g_build_slots.clear();
    rebuild_modules_from_world();

    g_base_x = x0 + 8;
    g_base_y = cz - 1;
    spawn_player_at_base();
    g_cam_pos = g_player.pos;
    set_toast("Mapa de teste de fisica carregado (F6).", 4.0f);
}

// get_block_height/surface_block_at/object_block_at/surface_height_at/is_mineable/
// block_hits_required moved to world.cpp (declarations now in world.h); called from
// many places below unrelated to World generation itself (physics, mining, rendering).


// TerrainPhysicsProfile/GroundProbeResult structs and terrain_type_from_block()...
// step_player_physics() moved to player_physics.h/.cpp (this is the player/physics
// extraction stage). reload_camera_config() below is unrelated to this move (it stays
// here, see comment on its own declaration in config_io.h for why).
// reload_camera_config: special case, NOT moved into config_io.cpp with the other 5
// reload_*_config wrappers. See the comment on its declaration in config_io.h for the
// full reasoning — in short, after loading CameraConfig it also re-clamps the live
// g_camera object. Camera3D (g_camera's type) has since moved to camera.h/.cpp (this
// phase of the plan), so the original blocker (config_io.cpp having no complete Camera3D
// definition) is gone; reload_camera_config just hasn't been relocated too, since doing
// so isn't needed for the camera extraction itself. It still reuses the generic
// reload_config<Cfg> template from config_io.h for the path-search/read/apply-overrides
// plumbing, same as the other 5.
bool reload_camera_config(bool create_if_missing) {
    bool loaded = reload_config<CameraConfig>("camera_config.json", g_camera_cfg, create_if_missing,
                                               write_default_camera_config, apply_camera_config_overrides,
                                               &g_camera_config_path);
    g_camera.distance = std::clamp(g_camera.distance, g_camera.min_distance, g_camera.max_distance);
    g_camera.pitch = std::clamp(g_camera.pitch, g_camera.min_pitch, g_camera.max_pitch);
    return loaded;
}


// ============= Crafting =============
// ============================================================================
// MODULE & RESOURCE SYSTEM - Complete Gameplay Loop
// ============================================================================

// CraftCost struct moved to inventory_crafting.h/.cpp (verbatim) - this is the
// items_particles/modules_building/inventory_crafting extraction stage.
// inventory_crafting.h (included at the top of this file) supplies it.

// ModuleStats struct moved to modules_building.h/.cpp (verbatim) - same stage.
// modules_building.h (included at the top of this file) supplies it.

// Construction in progress
// Note: ConstructionJob, Alert, g_construction_queue, g_alerts, g_base_integrity
// are declared earlier in the file with forward declarations

static constexpr float kBaseIntegrityMax = 100.0f;
static constexpr float kBaseIntegrityDecayRate = 0.5f;  // Per minute without workshop

// Cooldown para evitar spam de alertas. Lost "static": modules_building.cpp's
// update_modules() (extracted from this file) now iterates it (cooldown countdown) from
// another translation unit - same pattern as g_oxygen/g_water_res/etc. in textures.cpp.
std::unordered_map<std::string, float> g_alert_cooldowns;

// Lost "static": modules_building.cpp's start_construction()/update_modules() (extracted
// from this file) now call it from another translation unit - same pattern as
// g_oxygen/g_water_res/etc. in textures.cpp.
void add_alert(const std::string& msg, float r, float g, float b, float duration = 3.0f, float cooldown = 5.0f) {
    // Check cooldown
    auto it = g_alert_cooldowns.find(msg);
    if (it != g_alert_cooldowns.end() && it->second > 0.0f) {
        return;  // Still on cooldown
    }
    
    // Don't duplicate alerts
    for (auto& a : g_alerts) {
        if (a.message == msg) {
            a.time_remaining = duration;
            return;
        }
    }
    g_alerts.push_back({msg, r, g, b, duration});
    g_alert_cooldowns[msg] = cooldown;
}

// get_module_stats() moved to modules_building.h/.cpp (verbatim) - this is the
// items_particles/modules_building/inventory_crafting extraction stage.
// modules_building.h (included at the top of this file) supplies its
// declaration.

// get_module_cost() moved to inventory_crafting.h/.cpp (verbatim) - same stage.
// inventory_crafting.h (included at the top of this file) supplies it.

// can_afford()/spend_cost() forward declarations removed: both moved to
// inventory_crafting.h/.cpp (verbatim, with can_afford/spend_cost/refund_cost
// deduplicated into a single pointer-to-member table - see the comment above
// can_afford() in inventory_crafting.cpp), so inventory_crafting.h (included at
// the top of this file) now supplies real declarations instead.

// module_cost_string() moved to inventory_crafting.h/.cpp (verbatim) - same stage.

// get_module_status()/status_string() moved to modules_building.cpp (verbatim,
// both stay static there - grep confirms neither is called anywhere outside this
// module, pre-existing dead code from before this refactor).

// start_construction() moved to modules_building.h/.cpp (verbatim) -
// modules_building.h supplies its declaration.

// UnlockRequirement struct + get_unlock_requirement() moved to
// modules_building.cpp (verbatim, both stay file-local/static there - only used
// internally by is_unlocked/check_unlocks/unlock_progress_string, all in the same
// file).

// is_unlocked()/check_unlocks()/unlock_progress_string() moved to
// modules_building.h/.cpp (verbatim) - modules_building.h supplies their
// declarations. check_unlocks() needs this external linkage for a new reason beyond
// main.cpp's own call site: items_particles.cpp's on_pickup_item() now also calls it,
// from another translation unit.

// module_cost()/cost_string() moved to inventory_crafting.h/.cpp (verbatim) -
// inventory_crafting.h supplies their declarations. NOTE: module_cost()/
// get_module_cost() and cost_string()/module_cost_string() are two pre-existing,
// similarly-named-but-different function pairs (one for the instant right-click
// placement path, one for the build-menu/construction-queue path) - this stage
// preserves both as-is, it does not merge them (see plan).

// can_afford()/spend_cost()/refund_cost() moved to inventory_crafting.h/.cpp,
// WITH the one deliberate behavior-preserving change of this stage: the three
// near-identical 10-line bodies (each hand-listing the same 10 CraftCost fields
// against g_inventory[(int)Block::X]) are now a single pointer-to-member table
// iterated by all three - see inventory_crafting.cpp for the verification notes.

// spawn_block_particles()/drop_item_for_block()/drop_spawn_y_for_block()/
// spawn_item_drop()/on_pickup_item()/update_item_drops() moved to
// items_particles.h/.cpp (verbatim) - this is the items_particles/
// modules_building/inventory_crafting extraction stage. items_particles.h
// (included at the top of this file) supplies the declarations this file relies
// on (raycast mining/placement in update_game, further below). on_pickup_item()
// is not declared there - it is only called internally by update_item_drops()
// (which moved to the same file), so it stays static inside items_particles.cpp.

// ============= OpenGL Setup =============
static HGLRC setup_opengl(HDC hdc) {
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int format = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, format, &pfd);

    HGLRC hrc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, hrc);

    glDisable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Atlas pixel-art (Minicraft/Minecraft-like)
    init_texture_atlas();
    glBindTexture(GL_TEXTURE_2D, 0);
    return hrc;
}

// render_quad/render_quad_tex/render_bar/render_circle/render_ellipse/render_rounded_rect
// (2D primitives) and render_player_topdown/render_astronaut (top-down player rendering,
// interleaved with the primitives right here in the original file) moved out as part of the
// render_primitives/render_player extraction stage: the first group to render_primitives.h/
// .cpp (included at the top of this file), the player pair to render_player.h/.cpp (also
// included at the top - it depends on both Player from player_physics.h and on
// render_primitives' render_circle/render_ellipse/render_quad).
// try_spawn_tree/terraform_step/recompute_terraform_score/update_phase/melt_ice_around
// moved to world.cpp (declarations now in world.h).

// update_shooting_stars() forward declaration removed from here: update_modules()
// (the only caller in this file, back when this comment was written) moved out of this
// file first, then update_shooting_stars() itself moved to sky.h/.cpp (verbatim) - the sky
// extraction stage. modules_building.cpp's update_modules() keeps calling it exactly as
// before, now via sky.h's declaration instead of its own forward declaration's original
// target in this file.

// update_modules() moved to modules_building.h/.cpp (verbatim) - this is the
// items_particles/modules_building/inventory_crafting extraction stage.
// modules_building.h (included at the top of this file) supplies its
// declaration; update_game() (further below) calls it exactly as before.

// ============= Renderizacao 3D (Estilo Minicraft) =============

// render_cube_outline_3d/render_cube_3d moved to render_primitives.h/.cpp (render_primitives
// extraction stage) - render_cube_3d_tex below stays here for now (not part of that stage's
// list) and keeps calling render_cube_outline_3d via render_primitives.h's declaration.

// Renderizar cubo 3D texturizado (tile do atlas) com iluminacao fake por face.
// Requer GL_TEXTURE_2D habilitado e g_tex_atlas bindado.
static void render_cube_3d_tex(float x, float y, float z, float size, Tile top, Tile side, Tile bottom,
                               float tint_r, float tint_g, float tint_b, float a = 1.0f, bool outline = false) {
    float half = size * 0.5f;

    // Iluminacao fake (3 niveis)
    float top_shade = 1.00f;
    float side_shade = 0.72f;
    float dark_shade = 0.52f;

    UvRect uv_top = atlas_uv(top);
    UvRect uv_side = atlas_uv(side);
    UvRect uv_bottom = atlas_uv(bottom);

    glBegin(GL_QUADS);

    // Top (Y+)
    glColor4f(tint_r * top_shade, tint_g * top_shade, tint_b * top_shade, a);
    glTexCoord2f(uv_top.u0, uv_top.v1); glVertex3f(x - half, y + half, z - half);
    glTexCoord2f(uv_top.u1, uv_top.v1); glVertex3f(x + half, y + half, z - half);
    glTexCoord2f(uv_top.u1, uv_top.v0); glVertex3f(x + half, y + half, z + half);
    glTexCoord2f(uv_top.u0, uv_top.v0); glVertex3f(x - half, y + half, z + half);

    // Bottom (Y-)
    glColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
    glTexCoord2f(uv_bottom.u0, uv_bottom.v0); glVertex3f(x - half, y - half, z + half);
    glTexCoord2f(uv_bottom.u1, uv_bottom.v0); glVertex3f(x + half, y - half, z + half);
    glTexCoord2f(uv_bottom.u1, uv_bottom.v1); glVertex3f(x + half, y - half, z - half);
    glTexCoord2f(uv_bottom.u0, uv_bottom.v1); glVertex3f(x - half, y - half, z - half);

    // Front (Z+)
    glColor4f(tint_r * side_shade, tint_g * side_shade, tint_b * side_shade, a);
    glTexCoord2f(uv_side.u0, uv_side.v0); glVertex3f(x - half, y - half, z + half);
    glTexCoord2f(uv_side.u1, uv_side.v0); glVertex3f(x + half, y - half, z + half);
    glTexCoord2f(uv_side.u1, uv_side.v1); glVertex3f(x + half, y + half, z + half);
    glTexCoord2f(uv_side.u0, uv_side.v1); glVertex3f(x - half, y + half, z + half);

    // Back (Z-)
    glColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
    glTexCoord2f(uv_side.u0, uv_side.v0); glVertex3f(x + half, y - half, z - half);
    glTexCoord2f(uv_side.u1, uv_side.v0); glVertex3f(x - half, y - half, z - half);
    glTexCoord2f(uv_side.u1, uv_side.v1); glVertex3f(x - half, y + half, z - half);
    glTexCoord2f(uv_side.u0, uv_side.v1); glVertex3f(x + half, y + half, z - half);

    // Left (X-)
    glColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
    glTexCoord2f(uv_side.u0, uv_side.v0); glVertex3f(x - half, y - half, z - half);
    glTexCoord2f(uv_side.u1, uv_side.v0); glVertex3f(x - half, y - half, z + half);
    glTexCoord2f(uv_side.u1, uv_side.v1); glVertex3f(x - half, y + half, z + half);
    glTexCoord2f(uv_side.u0, uv_side.v1); glVertex3f(x - half, y + half, z - half);

    // Right (X+)
    glColor4f(tint_r * side_shade, tint_g * side_shade, tint_b * side_shade, a);
    glTexCoord2f(uv_side.u0, uv_side.v0); glVertex3f(x + half, y - half, z + half);
    glTexCoord2f(uv_side.u1, uv_side.v0); glVertex3f(x + half, y - half, z - half);
    glTexCoord2f(uv_side.u1, uv_side.v1); glVertex3f(x + half, y + half, z - half);
    glTexCoord2f(uv_side.u0, uv_side.v1); glVertex3f(x + half, y + half, z + half);

    glEnd();

    if (outline) {
        render_cube_outline_3d(x, y, z, size, 1.0f);
    }
}

// light_attenuation()/get_module_light()/compute_ambient_light()/get_natural_light_color()/
// collect_lights()/compute_shadow()/world_to_lightmap_index()/add_light_to_lightmap()/
// blur_lightmap_pass()/blur_lightmap()/extract_bloom()/blur_bloom()/compute_lightmap()/
// sample_lightmap()/compute_depth_factor()/apply_color_grading()/compute_vignette() (and the
// smoothstep() helper) moved to lighting.h/.cpp (verbatim) - the lighting extraction stage.
// lighting.h supplies the four declarations render_world() below still needs
// (compute_lightmap/sample_lightmap/compute_depth_factor/apply_color_grading); everything
// else stays static inside lighting.cpp (grep confirms no call sites outside that file) -
// see the comments there for exactly which pattern applies to each. compute_vignette() in
// particular is grep-confirmed dead code (defined, never called anywhere in the codebase),
// kept static and unchanged.

// SkyPalette struct, compute_sky_palette()/render_sky_gradient_dome()/render_billboard_disc()/
// render_lit_sphere()/render_star_layer()/render_nebula_layer()/render_cloud_layer()/
// update_shooting_stars()/render_shooting_stars()/render_alien_sky() (and the hash01()
// helper) moved to sky.h/.cpp (verbatim) - the sky extraction stage. sky.h (included at the
// top of this file) supplies the three declarations still needed from outside that file:
// SkyPalette/compute_sky_palette() (render_world() below builds its own SkyPalette for the
// GL clear color) and render_alien_sky() (render_world()'s single call site, right after).
// update_shooting_stars() is declared there too - modules_building.cpp's update_modules()
// keeps calling it exactly as before. Everything else stays static inside sky.cpp.
// Renderizar plano horizontal 3D (para chao/agua)
static void render_plane_3d(float x, float y, float z, float size, float r, float g, float b, float a = 1.0f) {
    float half = size * 0.5f;
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex3f(x - half, y, z - half);
    glVertex3f(x + half, y, z - half);
    glVertex3f(x + half, y, z + half);
    glVertex3f(x - half, y, z + half);
    glEnd();
}

// Renderizar plano texturizado (tile do atlas). Requer GL_TEXTURE_2D habilitado.
static void render_plane_3d_tex(float x, float y, float z, float size, Tile tile,
                                float tint_r, float tint_g, float tint_b, float a = 1.0f) {
    float half = size * 0.5f;
    UvRect uv = atlas_uv(tile);
    glColor4f(tint_r, tint_g, tint_b, a);
    glBegin(GL_QUADS);
    glTexCoord2f(uv.u0, uv.v0); glVertex3f(x - half, y, z - half);
    glTexCoord2f(uv.u1, uv.v0); glVertex3f(x + half, y, z - half);
    glTexCoord2f(uv.u1, uv.v1); glVertex3f(x + half, y, z + half);
    glTexCoord2f(uv.u0, uv.v1); glVertex3f(x - half, y, z + half);
    glEnd();
}

// render_wall_3d_tex_{xpos,xneg,zpos,zneg}/render_sphere_3d moved to render_primitives.h/
// .cpp (render_primitives extraction stage). The 4 near-identical wall functions were
// collapsed there into a single render_wall_3d_tex(WallFace face, ...) parameterized by
// face - the 4 call sites in render_world() below were updated to the new form (this is
// the one deliberate non-verbatim call-site change in this stage, per the refactor plan's
// Fase 1b wall dedup).

// Renderizar cilindro 3D (para corpo do player)
static void render_cylinder_3d(float cx, float cy, float cz, float radius, float height, float r, float g, float b, float a = 1.0f, int segments = 12) {
    float half_h = height * 0.5f;
    
    // Corpo do cilindro
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * kPi * (float)i / segments;
        float x = std::cos(angle);
        float z = std::sin(angle);
        float shade = 0.7f + 0.3f * std::fabs(x);  // Sombreamento lateral
        glColor4f(r * shade, g * shade, b * shade, a);
        glVertex3f(cx + radius * x, cy + half_h, cz + radius * z);
        glVertex3f(cx + radius * x, cy - half_h, cz + radius * z);
    }
    glEnd();
    
    // Topo
    glColor4f(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(cx, cy + half_h, cz);
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * kPi * (float)i / segments;
        glVertex3f(cx + radius * std::cos(angle), cy + half_h, cz + radius * std::sin(angle));
    }
    glEnd();
}

static void render_physics_debug_3d() {
    if (!g_debug) return;

    Vec2 rp = get_player_render_pos();
    float ry = get_player_render_y();
    float hw = g_player.w * 0.5f;
    float hd = g_player.h * 0.5f;
    float foot = ry + g_physics_cfg.collision_skin;
    float head = foot + g_physics_cfg.collider_height;

    glDisable(GL_TEXTURE_2D);
    glLineWidth(1.8f);

    // Collider AABB.
    glColor4f(0.10f, 0.95f, 1.0f, 0.95f);
    glBegin(GL_LINE_LOOP);
    glVertex3f(rp.x - hw, foot, rp.y - hd);
    glVertex3f(rp.x + hw, foot, rp.y - hd);
    glVertex3f(rp.x + hw, foot, rp.y + hd);
    glVertex3f(rp.x - hw, foot, rp.y + hd);
    glEnd();

    glBegin(GL_LINE_LOOP);
    glVertex3f(rp.x - hw, head, rp.y - hd);
    glVertex3f(rp.x + hw, head, rp.y - hd);
    glVertex3f(rp.x + hw, head, rp.y + hd);
    glVertex3f(rp.x - hw, head, rp.y + hd);
    glEnd();

    glBegin(GL_LINES);
    glVertex3f(rp.x - hw, foot, rp.y - hd); glVertex3f(rp.x - hw, head, rp.y - hd);
    glVertex3f(rp.x + hw, foot, rp.y - hd); glVertex3f(rp.x + hw, head, rp.y - hd);
    glVertex3f(rp.x + hw, foot, rp.y + hd); glVertex3f(rp.x + hw, head, rp.y + hd);
    glVertex3f(rp.x - hw, foot, rp.y + hd); glVertex3f(rp.x - hw, head, rp.y + hd);
    glEnd();

    // Ground rays.
    for (int i = 0; i < g_physics.debug_ray_count; ++i) {
        const PhysicsRayDebug& ray = g_physics.debug_rays[(size_t)i];
        if (ray.hit) glColor4f(0.20f, 1.0f, 0.30f, 0.90f);
        else glColor4f(1.0f, 0.20f, 0.20f, 0.90f);
        glBegin(GL_LINES);
        glVertex3f(ray.from.x, ray.from.y, ray.from.z);
        glVertex3f(ray.to.x, ray.to.y, ray.to.z);
        glEnd();
    }

    // Camera rays (obstruction checks).
    for (int i = 0; i < g_camera_debug_ray_count; ++i) {
        const CameraDebugRay& ray = g_camera_debug_rays[(size_t)i];
        if (ray.blocked) glColor4f(1.0f, 0.35f, 0.20f, 0.92f);
        else glColor4f(0.35f, 0.78f, 1.0f, 0.85f);
        glBegin(GL_LINES);
        glVertex3f(ray.from.x, ray.from.y, ray.from.z);
        glVertex3f(ray.to.x, ray.to.y, ray.to.z);
        glEnd();
    }

    // Ground normal.
    Vec3 n0 = {rp.x, g_player.ground_height + 0.03f, rp.y};
    Vec3 n1 = {n0.x + g_physics.ground_normal.x * 1.1f,
               n0.y + g_physics.ground_normal.y * 1.1f,
               n0.z + g_physics.ground_normal.z * 1.1f};
    glColor4f(0.30f, 0.70f, 1.0f, 1.0f);
    glBegin(GL_LINES);
    glVertex3f(n0.x, n0.y, n0.z);
    glVertex3f(n1.x, n1.y, n1.z);
    glEnd();

    // Velocity vector.
    Vec3 v0 = {rp.x, ry + 0.90f, rp.y};
    Vec3 v1 = {
        v0.x + g_player.vel.x * 0.20f,
        v0.y + g_player.vel_y * 0.10f,
        v0.z + g_player.vel.y * 0.20f
    };
    glColor4f(1.0f, 0.85f, 0.25f, 1.0f);
    glBegin(GL_LINES);
    glVertex3f(v0.x, v0.y, v0.z);
    glVertex3f(v1.x, v1.y, v1.z);
    glEnd();

    // Collision normal.
    if (g_physics.hit_x || g_physics.hit_z) {
        Vec3 c0 = {rp.x, foot + 0.15f, rp.y};
        Vec3 c1 = {c0.x + g_physics.collision_normal.x * 0.7f, c0.y, c0.z + g_physics.collision_normal.y * 0.7f};
        glColor4f(1.0f, 0.2f, 1.0f, 1.0f);
        glBegin(GL_LINES);
        glVertex3f(c0.x, c0.y, c0.z);
        glVertex3f(c1.x, c1.y, c1.z);
        glEnd();
    }

    glLineWidth(1.0f);
}

// ============= Rendering =============
static void render_world(HDC hdc, int win_w, int win_h) {
    if (!g_world) return;

    // === SETUP 3D ===
    glViewport(0, 0, win_w, win_h);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Projecao perspectiva
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    float aspect = (float)win_w / (float)win_h;
    apply_perspective(74.0f, aspect, 0.1f, 2200.0f);
    
    // Atualizar camera (target + colisao) para o frame atual
    update_camera_for_frame();
    
    // Aplicar view matrix
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    apply_look_at();

    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float atmos_factor = clamp01(g_atmosphere / 100.0f);
    SkyPalette sky_palette = compute_sky_palette(day_phase, atmos_factor);
    float sky_r = lerp(sky_palette.hz_r, sky_palette.zn_r, 0.35f);
    float sky_g = lerp(sky_palette.hz_g, sky_palette.zn_g, 0.35f);
    float sky_b = lerp(sky_palette.hz_b, sky_palette.zn_b, 0.35f);
    
    // Clear com cor do ceu alienigena
    glClearColor(sky_r, sky_g, sky_b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // === RENDERIZAR ELEMENTOS DO CEU (sol, luas, estrelas, anel) ===
    render_alien_sky(g_camera.position.x, g_camera.position.y, g_camera.position.z, day_phase, atmos_factor);

    // === COMPUTAR LIGHTMAP 2D (RTX FAKE) ===
    compute_lightmap();

    // Calcular area visivel baseada na posicao do jogador (culling)
    Vec2 rpos = get_player_render_pos();
    float rpy = get_player_render_y();
    int player_tile_x = world_to_tile(rpos.x);
    int player_tile_z = world_to_tile(rpos.y);  // Y do 2D = Z no 3D
    int view_radius = (int)std::clamp(g_camera.distance * 3.8f + 55.0f, 110.0f, 200.0f);
    int wall_radius = std::clamp(view_radius - 45, 80, view_radius);
    int obj_radius = std::clamp(view_radius - 30, 90, view_radius);
    int view_radius2 = view_radius * view_radius;
    int wall_radius2 = wall_radius * wall_radius;
    int obj_radius2 = obj_radius * obj_radius;

    // Fog de distancia por bioma para profundidade e esconder limite do mapa.
    {
        Block fog_surface = Block::Dirt;
        if (g_world->in_bounds(player_tile_x, player_tile_z)) {
            fog_surface = surface_block_at(*g_world, player_tile_x, player_tile_z);
        }

        float fog_mul_r = 1.0f, fog_mul_g = 1.0f, fog_mul_b = 1.0f;
        float fog_start_mul = 1.0f, fog_end_mul = 1.0f;
        switch (fog_surface) {
            case Block::Ice:
            case Block::Snow:
                fog_mul_r = 0.95f; fog_mul_g = 1.02f; fog_mul_b = 1.12f;
                fog_start_mul = 0.86f; fog_end_mul = 0.86f;
                break;
            case Block::Sand:
                fog_mul_r = 1.08f; fog_mul_g = 1.00f; fog_mul_b = 0.86f;
                fog_start_mul = 0.92f; fog_end_mul = 0.93f;
                break;
            case Block::Stone:
            case Block::Coal:
            case Block::Iron:
                fog_mul_r = 0.88f; fog_mul_g = 0.92f; fog_mul_b = 0.98f;
                fog_start_mul = 0.84f; fog_end_mul = 0.88f;
                break;
            case Block::Water:
                fog_mul_r = 0.82f; fog_mul_g = 0.95f; fog_mul_b = 1.08f;
                fog_start_mul = 0.80f; fog_end_mul = 0.84f;
                break;
            default:
                break;
        }

        float fog_col[4] = {
            clamp01(sky_r * fog_mul_r),
            clamp01(sky_g * fog_mul_g),
            clamp01(sky_b * fog_mul_b),
            1.0f
        };
        glEnable(GL_FOG);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogfv(GL_FOG_COLOR, fog_col);
        glHint(GL_FOG_HINT, GL_NICEST);

        float fog_start = std::max(70.0f, (float)view_radius * g_sky_cfg.fog_start_factor * fog_start_mul);
        float fog_end = std::max(fog_start + 110.0f,
                                 (float)view_radius * g_sky_cfg.fog_end_factor * fog_end_mul + g_sky_cfg.fog_distance_bonus);
        glFogf(GL_FOG_START, fog_start);
        glFogf(GL_FOG_END, fog_end);
    }

    // === RENDERIZACAO 3D DO MUNDO ===
    
    int start_x = std::max(0, player_tile_x - view_radius);
    int end_x = std::min(g_world->w - 1, player_tile_x + view_radius);
    int start_z = std::max(0, player_tile_z - view_radius);
    int end_z = std::min(g_world->h - 1, player_tile_z + view_radius);
    
    // Texturas
    bool use_textures = (g_tex_atlas != 0);
    if (use_textures) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, g_tex_atlas);
    } else {
        glDisable(GL_TEXTURE_2D);
    }
    int water_frame = ((int)std::floor(g_day_time * 4.0f)) & 3;

    // Renderizar terreno com altura (montanhas/vales/desfiladeiros) + objetos sobre o solo
    {
        constexpr float side_shade = 0.72f;
        constexpr float dark_shade = 0.52f;
        constexpr float kTopEps = 0.01f;

        for (int tz = start_z; tz <= end_z; ++tz) {
            for (int tx = start_x; tx <= end_x; ++tx) {
                int ddx = tx - player_tile_x;
                int ddz = tz - player_tile_z;
                int dist2 = ddx * ddx + ddz * ddz;
                if (dist2 > view_radius2) continue; // culling circular

                float base_y = (float)g_world->height_at(tx, tz) * kHeightScale;

                Block surface = surface_block_at(*g_world, tx, tz);
                Block obj = object_block_at(*g_world, tx, tz);

                float world_x = (float)tx;
                float world_z = (float)tz;

                // === SOLO (top) ===
                {
                    BlockTex gtex = block_tex(surface);
                    if (gtex.is_water) {
                        gtex.top = (Tile)((int)Tile::Water0 + water_frame);
                        gtex.side = gtex.top;
                        gtex.bottom = gtex.top;
                    }

                    float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, a = 1.0f;
                    if (gtex.uses_tint || gtex.transparent) {
                        float cr, cg, cb, ca;
                        block_color(surface, tz, g_world->h, cr, cg, cb, ca);
                        if (gtex.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                        if (gtex.transparent) a = ca;
                    }
                    a *= camera_occluder_alpha_for_tile(tx, tz);

                    // Edge blending entre terrenos adjacentes (transicao visual suave).
                    float neigh_r = 0.0f, neigh_g = 0.0f, neigh_b = 0.0f;
                    int neigh_count = 0;
                    int diff_count = 0;
                    const int nx[4] = {1, -1, 0, 0};
                    const int nz[4] = {0, 0, 1, -1};
                    for (int ni = 0; ni < 4; ++ni) {
                        int sx = tx + nx[ni];
                        int sz = tz + nz[ni];
                        if (!g_world->in_bounds(sx, sz)) continue;
                        Block sb = surface_block_at(*g_world, sx, sz);
                        BlockTex sbtex = block_tex(sb);
                        float sr = 1.0f, sg = 1.0f, sbb = 1.0f;
                        if (sbtex.uses_tint || sbtex.transparent) {
                            float cr, cg, cb, ca;
                            block_color(sb, sz, g_world->h, cr, cg, cb, ca);
                            if (sbtex.uses_tint) { sr = cr; sg = cg; sbb = cb; }
                        }
                        neigh_r += sr;
                        neigh_g += sg;
                        neigh_b += sbb;
                        neigh_count++;
                        if (sb != surface) diff_count++;
                    }
                    if (neigh_count > 0 && diff_count > 0) {
                        float inv = 1.0f / (float)neigh_count;
                        neigh_r *= inv;
                        neigh_g *= inv;
                        neigh_b *= inv;
                        float edge_blend = ((float)diff_count / 4.0f) * 0.34f;
                        tint_r = lerp(tint_r, neigh_r, edge_blend);
                        tint_g = lerp(tint_g, neigh_g, edge_blend);
                        tint_b = lerp(tint_b, neigh_b, edge_blend);
                    }

                    float h_here = base_y;
                    float h_e = (tx < g_world->w - 1) ? (float)g_world->height_at(tx + 1, tz) * kHeightScale : h_here;
                    float h_w = (tx > 0) ? (float)g_world->height_at(tx - 1, tz) * kHeightScale : h_here;
                    float h_s = (tz < g_world->h - 1) ? (float)g_world->height_at(tx, tz + 1) * kHeightScale : h_here;
                    float h_n = (tz > 0) ? (float)g_world->height_at(tx, tz - 1) * kHeightScale : h_here;

                    // Shading leve por inclinacao/altura para destacar montanhas/vales.
                    float dhx = h_e - h_w;
                    float dhz = h_s - h_n;
                    float slope = std::sqrt(dhx * dhx + dhz * dhz);
                    float slope_shade = 1.0f - std::clamp(slope * 0.22f, 0.0f, 0.28f);
                    float alt_shade = 0.90f + 0.10f * clamp01(base_y / 18.0f);
                    float shade = slope_shade * alt_shade;
                    tint_r *= shade;
                    tint_g *= shade;
                    tint_b *= shade;
                    
                    // === ILUMINACAO 2D (RTX FAKE) ===
                    if (g_lighting.enabled) {
                        float light_r, light_g, light_b;
                        sample_lightmap((float)tx, (float)tz, light_r, light_g, light_b);
                        
                        // Escurecimento por profundidade
                        float depth_factor = compute_depth_factor(base_y, rpy);
                        light_r *= depth_factor;
                        light_g *= depth_factor;
                        light_b *= depth_factor;
                        
                        // Aplicar iluminacao
                        tint_r *= light_r;
                        tint_g *= light_g;
                        tint_b *= light_b;
                        
                        // Color grading
                        apply_color_grading(tint_r, tint_g, tint_b);
                    }

                    if (surface == Block::Water) {
                        float water_y = base_y - 0.18f + 0.05f * std::sin(g_day_time * 2.0f + world_x * 0.5f + world_z * 0.3f);
                        if (use_textures) render_plane_3d_tex(world_x, water_y, world_z, 1.0f, gtex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, water_y, world_z, 1.0f, tint_r, tint_g, tint_b, 0.75f);
                    } else {
                        float top_y = base_y + kTopEps;
                        if (use_textures) render_plane_3d_tex(world_x, top_y, world_z, 1.0f, gtex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, top_y, world_z, 1.0f, tint_r, tint_g, tint_b, a);
                    }

                    // === LATERAIS (paredes) para diferenca de altura ===
                    bool do_walls = (dist2 <= wall_radius2);
                    if (!do_walls) {
                        float max_drop = std::max(std::max(h_here - h_e, h_here - h_w), std::max(h_here - h_s, h_here - h_n));
                        if (max_drop > 1.40f) do_walls = true; // manter grandes penhascos visiveis ao longe
                    }

                    if (do_walls) {
                        if (use_textures) {
                            if (h_e < h_here) render_wall_3d_tex(WallFace::XPos, world_x, world_z, h_e, h_here, gtex.side, tint_r, tint_g, tint_b, a, side_shade);
                            if (h_w < h_here) render_wall_3d_tex(WallFace::XNeg, world_x, world_z, h_w, h_here, gtex.side, tint_r, tint_g, tint_b, a, dark_shade);
                            if (h_s < h_here) render_wall_3d_tex(WallFace::ZPos, world_x, world_z, h_s, h_here, gtex.side, tint_r, tint_g, tint_b, a, side_shade);
                            if (h_n < h_here) render_wall_3d_tex(WallFace::ZNeg, world_x, world_z, h_n, h_here, gtex.side, tint_r, tint_g, tint_b, a, dark_shade);
                        } else {
                            // Fallback sem texturas: quads coloridos
                            auto wall_col = [&](float s) {
                                glColor4f(tint_r * s, tint_g * s, tint_b * s, a);
                            };
                            constexpr float half = 0.5f;
                            if (h_e < h_here) {
                                wall_col(side_shade);
                                glBegin(GL_QUADS);
                                glVertex3f(world_x + half, h_e, world_z - half);
                                glVertex3f(world_x + half, h_e, world_z + half);
                                glVertex3f(world_x + half, h_here, world_z + half);
                                glVertex3f(world_x + half, h_here, world_z - half);
                                glEnd();
                            }
                            if (h_w < h_here) {
                                wall_col(dark_shade);
                                glBegin(GL_QUADS);
                                glVertex3f(world_x - half, h_w, world_z + half);
                                glVertex3f(world_x - half, h_w, world_z - half);
                                glVertex3f(world_x - half, h_here, world_z - half);
                                glVertex3f(world_x - half, h_here, world_z + half);
                                glEnd();
                            }
                            if (h_s < h_here) {
                                wall_col(side_shade);
                                glBegin(GL_QUADS);
                                glVertex3f(world_x - half, h_s, world_z + half);
                                glVertex3f(world_x + half, h_s, world_z + half);
                                glVertex3f(world_x + half, h_here, world_z + half);
                                glVertex3f(world_x - half, h_here, world_z + half);
                                glEnd();
                            }
                            if (h_n < h_here) {
                                wall_col(dark_shade);
                                glBegin(GL_QUADS);
                                glVertex3f(world_x + half, h_n, world_z - half);
                                glVertex3f(world_x - half, h_n, world_z - half);
                                glVertex3f(world_x - half, h_here, world_z - half);
                                glVertex3f(world_x + half, h_here, world_z - half);
                                glEnd();
                            }
                        }
                    }
                }

                // === OBJETOS sobre o solo (rochas/minerios/modulos/estruturas) ===
                if (obj != Block::Air && dist2 <= obj_radius2) {
                    BlockTex tex = block_tex(obj);
                    if (tex.is_water) {
                        tex.top = (Tile)((int)Tile::Water0 + water_frame);
                        tex.side = tex.top;
                        tex.bottom = tex.top;
                    }

                    float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, a = 1.0f;
                    if (tex.uses_tint || tex.transparent) {
                        float cr, cg, cb, ca;
                        block_color(obj, tz, g_world->h, cr, cg, cb, ca);
                        if (tex.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                        if (tex.transparent) a = ca;
                    }
                    a *= camera_occluder_alpha_for_tile(tx, tz);
                    
                    // === ILUMINACAO 2D PARA OBJETOS (RTX FAKE) ===
                    if (g_lighting.enabled) {
                        float light_r, light_g, light_b;
                        sample_lightmap((float)tx, (float)tz, light_r, light_g, light_b);
                        
                        // Objetos emissivos (modulos, cristais) recebem boost de luz
                        bool is_emissive = is_module(obj) || obj == Block::Crystal;
                        if (is_emissive) {
                            light_r = std::max(light_r, 0.7f);
                            light_g = std::max(light_g, 0.7f);
                            light_b = std::max(light_b, 0.7f);
                        }
                        
                        // Escurecimento por profundidade
                        float depth_factor = compute_depth_factor(base_y, rpy);
                        light_r *= depth_factor;
                        light_g *= depth_factor;
                        light_b *= depth_factor;
                        
                        tint_r *= light_r;
                        tint_g *= light_g;
                        tint_b *= light_b;
                        
                        apply_color_grading(tint_r, tint_g, tint_b);
                    }

                    if (obj == Block::Leaves) {
                        // Folhas como plano elevado acima do terreno
                        float leaf_y = base_y + 0.60f;
                        if (use_textures) render_plane_3d_tex(world_x, leaf_y, world_z, 1.0f, tex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, leaf_y, world_z, 1.0f, tint_r, tint_g, tint_b, 0.85f);
                    } else if (obj == Block::Water) {
                        // Agua como plano levemente abaixo, com animacao
                        float water_y = base_y - 0.18f + 0.05f * std::sin(g_day_time * 2.0f + world_x * 0.5f + world_z * 0.3f);
                        if (use_textures) render_plane_3d_tex(world_x, water_y, world_z, 1.0f, tex.top, tint_r, tint_g, tint_b, a);
                        else render_plane_3d(world_x, water_y, world_z, 1.0f, tint_r, tint_g, tint_b, 0.75f);
                    } else {
                        bool use_outline = is_module(obj) || (obj == Block::Crystal || obj == Block::Coal || obj == Block::Iron || obj == Block::Copper);
                        float center_y = base_y + 0.5f;
                        if (use_textures) render_cube_3d_tex(world_x, center_y, world_z, 1.0f, tex.top, tex.side, tex.bottom, tint_r, tint_g, tint_b, a, use_outline);
                        else render_cube_3d(world_x, center_y, world_z, 1.0f, tint_r, tint_g, tint_b, a, use_outline);
                    }
                }
            }
        }
    }

    // Drops coletaveis
    if (use_textures && !g_drops.empty()) {
        for (size_t di = 0; di < g_drops.size(); ++di) {
            const auto& d = g_drops[di];
            // Culling simples no grid visivel
            if (d.x < (float)start_x - 2.0f || d.x >(float)end_x + 2.0f ||
                d.z < (float)start_z - 2.0f || d.z >(float)end_z + 2.0f) continue;

            BlockTex tex = block_tex(d.item);
            if (tex.is_water) {
                tex.top = (Tile)((int)Tile::Water0 + water_frame);
                tex.side = tex.top;
                tex.bottom = tex.top;
            }

            float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, a = 1.0f;
            if (tex.uses_tint || tex.transparent) {
                float cr, cg, cb, ca;
                block_color(d.item, (int)d.z, g_world->h, cr, cg, cb, ca);
                if (tex.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                if (tex.transparent) a = ca;
            }
            
            // Iluminacao 2D para drops
            if (g_lighting.enabled) {
                float light_r, light_g, light_b;
                sample_lightmap(d.x, d.z, light_r, light_g, light_b);
                tint_r *= light_r;
                tint_g *= light_g;
                tint_b *= light_b;
                apply_color_grading(tint_r, tint_g, tint_b);
            }

            bool aimed = ((int)di == g_target_drop);
            float bob = 0.03f * std::sin(d.t * 4.0f);
            float size = aimed ? 0.42f : 0.34f;
            float aa = aimed ? 1.0f : a;
            render_cube_3d_tex(d.x, d.y + bob, d.z, size, tex.top, tex.side, tex.bottom, tint_r, tint_g, tint_b, aa, true);
        }
    }

    if (use_textures) {
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }
    
    // === RENDERIZAR PLAYER 3D (Estilo Minicraft - Blocky) ===
    {
        float px = rpos.x;
        // OFFSET PARA ELEVAR O JOGADOR ACIMA DO SOLO (evita pes afundados)
        float player_y_offset = 0.15f;
        float py = rpy + player_y_offset;  // Altura real + offset
        float pz = rpos.y;  // Y do 2D = Z no 3D
        
        // Indicador de perigo (player pisca vermelho quando HP ou O2 baixo)
        bool in_danger = (g_player.hp < 30 || g_player_oxygen < 20.0f);
        float danger_pulse = in_danger ? (0.5f + 0.5f * std::sin(g_player.anim_frame * 8.0f)) : 0.0f;
        
        // Sombra no chao (maior e mais visivel)
        glDisable(GL_DEPTH_TEST);
        render_plane_3d(px, g_player.ground_height + 0.02f, pz, 0.9f, 0.0f, 0.0f, 0.0f, 0.55f);
        
        // Circulo de indicador de perigo
        if (in_danger) {
            render_plane_3d(px, g_player.ground_height + 0.03f, pz, 1.2f, 
                kColorDanger[0], kColorDanger[1], kColorDanger[2], danger_pulse * 0.3f);
        }
        glEnable(GL_DEPTH_TEST);
        
        // Usar rotacao continua para orientar o personagem
        float rot_rad = get_player_render_rotation() * (kPi / 180.0f);
        float sin_rot = std::sin(rot_rad);
        float cos_rot = std::cos(rot_rad);

        int surf_tx = world_to_tile(px);
        int surf_tz = world_to_tile(pz);
        Block surf = Block::Dirt;
        if (g_world && g_world->in_bounds(surf_tx, surf_tz)) {
            surf = surface_block_at(*g_world, surf_tx, surf_tz);
        }

        float temp_cold = smoothstep01(-35.0f, -65.0f, g_temperature);
        float frost = temp_cold * g_player_visual_cfg.suit_frost_strength;
        if (surf == Block::Snow || surf == Block::Ice) frost = std::min(1.0f, frost + 0.26f);
        float dirt = 0.0f;
        if (surf == Block::Dirt || surf == Block::Sand || surf == Block::Stone) dirt = g_player_visual_cfg.suit_dirt_strength;
        float damage = clamp01((100.0f - (float)g_player.hp) / 100.0f) * g_player_visual_cfg.suit_damage_strength;
        float wear = g_player_visual_cfg.suit_wear_strength;

        // Animacao de movimento (andar com peso + respiracao + idle).
        float breath = std::sin(g_player.anim_frame * g_player_visual_cfg.breathing_speed) * g_player_visual_cfg.breathing_amp;
        float idle_sway = std::sin(g_player.anim_frame * g_player_visual_cfg.idle_sway_speed) * g_player_visual_cfg.idle_sway_amp;
        float walk_wave = std::sin(g_player.walk_timer * g_player_visual_cfg.walk_bob_speed);
        float walk_bob = g_player.is_moving ? walk_wave * g_player_visual_cfg.walk_bob_amp : 0.0f;
        float walk_weight = g_player.is_moving ? std::fabs(std::sin(g_player.walk_timer * 0.5f * g_player_visual_cfg.walk_bob_speed)) * g_player_visual_cfg.walk_weight_amp : 0.0f;
        float mine_impact = g_player.is_mining ? g_player.mine_anim : 0.0f;
        float bob = breath + walk_bob - walk_weight - mine_impact * 0.06f;
        float leg_swing = g_player.is_moving ? walk_wave * 0.12f : 0.0f;

        float suit_r = 0.92f - wear * 0.12f - dirt * 0.16f - damage * 0.18f + frost * 0.12f;
        float suit_g = 0.93f - wear * 0.11f - dirt * 0.14f - damage * 0.17f + frost * 0.12f;
        float suit_b = 0.96f - wear * 0.08f - dirt * 0.10f - damage * 0.12f + frost * 0.16f;
        suit_r = clamp01(suit_r);
        suit_g = clamp01(suit_g);
        suit_b = clamp01(suit_b);
        
        // === CHAMA DO JETPACK (renderizar primeiro, atras do jogador) ===
        if (g_player.jetpack_active && g_player.jetpack_fuel > 0.0f) {
            float pack_dist = 0.25f;
            float flame_x = px - sin_rot * pack_dist;
            float flame_z = pz - cos_rot * pack_dist;
            
            // Animacao da chama (flicker)
            float flame_flicker = 0.8f + 0.4f * std::sin(g_player.jetpack_flame_anim * 2.0f);
            float flame_size = 0.15f + 0.05f * std::sin(g_player.jetpack_flame_anim * 3.0f);
            
            // Chama principal (laranja/amarela)
            for (int i = 0; i < 3; ++i) {
                float flame_y = py + 0.10f - i * 0.15f;
                float size = flame_size * (1.0f - i * 0.25f);
                float intensity = flame_flicker * (1.0f - i * 0.2f);
                
                // Nucleo amarelo
                render_cube_3d(flame_x, flame_y, flame_z, size * 0.6f, 
                    1.0f * intensity, 0.95f * intensity, 0.3f * intensity, 0.95f, false);
                // Chama laranja
                render_cube_3d(flame_x, flame_y - 0.08f, flame_z, size * 0.8f, 
                    1.0f * intensity, 0.55f * intensity, 0.1f * intensity, 0.85f, false);
                // Borda vermelha
                render_cube_3d(flame_x, flame_y - 0.15f, flame_z, size, 
                    0.95f * intensity, 0.25f * intensity, 0.05f * intensity, 0.7f, false);
            }
            
            // Particulas de fogo (pequenos cubos caindo)
            for (int i = 0; i < 4; ++i) {
                float particle_offset = std::sin(g_player.jetpack_flame_anim * 5.0f + i * 1.5f) * 0.08f;
                float particle_y = py - 0.1f - std::fmod(g_player.jetpack_flame_anim * 0.5f + i * 0.25f, 0.5f);
                float alpha = 0.8f - std::fmod(g_player.jetpack_flame_anim * 0.5f + i * 0.25f, 0.5f) * 1.5f;
                if (alpha > 0.0f) {
                    render_cube_3d(flame_x + particle_offset, particle_y, flame_z + particle_offset * 0.5f, 
                        0.06f, 1.0f, 0.6f, 0.1f, alpha, false);
                }
            }
        }
        
        // === CORPO (Bloco principal - torso branco do astronauta) ===
        render_cube_3d(px, py + 0.30f + bob, pz, 0.45f, suit_r, suit_g, suit_b, 1.0f, true);
        
        // === CABECA (Capacete - bloco branco com visor) ===
        render_cube_3d(px, py + 0.68f + bob, pz, 0.38f, suit_r * 0.98f, suit_g * 0.98f, suit_b, 1.0f, true);
        
        // Visor (bloco azul na frente da cabeca)
        float visor_dist = 0.12f;
        float vx = px + sin_rot * visor_dist;
        float vz = pz + cos_rot * visor_dist;
        render_cube_3d(vx, py + 0.68f + bob, vz, 0.22f, 0.10f, 0.35f, 0.75f, 0.95f, false);

        // Reflexo no visor.
        float refl_x = vx + sin_rot * 0.03f + cos_rot * 0.03f;
        float refl_z = vz + cos_rot * 0.03f - sin_rot * 0.03f;
        render_cube_3d(refl_x, py + 0.73f + bob, refl_z, 0.08f, 0.95f, 0.98f, 1.0f, g_player_visual_cfg.visor_reflect_alpha, false);
        
        // === MOCHILA (Bloco cinza atras) ===
        float pack_dist = 0.25f;
        float pack_x = px - sin_rot * pack_dist;
        float pack_z = pz - cos_rot * pack_dist;
        // Mochila brilha quando jetpack ativo
        float pack_r = 0.45f, pack_g = 0.47f, pack_b = 0.50f;
        if (g_player.jetpack_active) {
            pack_r = 0.55f; pack_g = 0.50f; pack_b = 0.45f;
        }
        render_cube_3d(pack_x, py + 0.35f + bob, pack_z, 0.30f, pack_r, pack_g, pack_b, 1.0f, true);

        // Tubos de oxigenio (mangueiras laterais da mochila ao torso).
        float tube_mid_x = px - sin_rot * 0.12f;
        float tube_mid_z = pz - cos_rot * 0.12f;
        float tube_side = 0.12f;
        float perp_x = cos_rot;
        float perp_z = -sin_rot;
        render_cube_3d(tube_mid_x - perp_x * tube_side, py + 0.36f + bob, tube_mid_z - perp_z * tube_side, 0.07f, 0.38f, 0.44f, 0.52f, 1.0f, false);
        render_cube_3d(tube_mid_x + perp_x * tube_side, py + 0.36f + bob, tube_mid_z + perp_z * tube_side, 0.07f, 0.38f, 0.44f, 0.52f, 1.0f, false);

        // Painel do peito.
        float chest_x = px + sin_rot * 0.16f;
        float chest_z = pz + cos_rot * 0.16f;
        render_cube_3d(chest_x, py + 0.30f + bob, chest_z, 0.12f, 0.10f, 0.14f, 0.18f, 0.98f, false);
        render_cube_3d(chest_x + perp_x * 0.030f, py + 0.31f + bob, chest_z + perp_z * 0.030f, 0.03f, 0.16f, 0.85f, 0.30f, 1.0f, false);
        render_cube_3d(chest_x - perp_x * 0.030f, py + 0.29f + bob, chest_z - perp_z * 0.030f, 0.03f, 0.90f, 0.24f, 0.18f, 1.0f, false);

        // Luz frontal do capacete.
        float lamp_x = px + sin_rot * 0.22f;
        float lamp_z = pz + cos_rot * 0.22f;
        float lamp_i = std::clamp(g_player_visual_cfg.headlamp_intensity, 0.0f, 2.0f);
        render_cube_3d(lamp_x, py + 0.80f + bob, lamp_z, 0.06f, 0.95f * lamp_i, 0.90f * lamp_i, 0.58f * lamp_i, 0.95f, false);
        glDisable(GL_DEPTH_TEST);
        render_plane_3d(lamp_x + sin_rot * 0.18f, py + 0.70f + bob, lamp_z + cos_rot * 0.18f, 0.42f, 1.0f, 0.92f, 0.68f, 0.20f * lamp_i);
        glEnable(GL_DEPTH_TEST);
        
        // === PERNAS (2 blocos pequenos animados) ===
        float leg_sep = 0.12f;
        
        // Perna esquerda
        float ll_x = px - perp_x * leg_sep + sin_rot * leg_swing + idle_sway * 0.4f;
        float ll_z = pz - perp_z * leg_sep + cos_rot * leg_swing;
        render_cube_3d(ll_x, py - 0.10f - walk_weight * 0.25f, ll_z, 0.18f, 0.25f, 0.27f, 0.30f, 1.0f, true);
        
        // Perna direita
        float rl_x = px + perp_x * leg_sep - sin_rot * leg_swing - idle_sway * 0.4f;
        float rl_z = pz + perp_z * leg_sep - cos_rot * leg_swing;
        render_cube_3d(rl_x, py - 0.10f - walk_weight * 0.25f, rl_z, 0.18f, 0.25f, 0.27f, 0.30f, 1.0f, true);
        
        // === BRACOS (2 blocos pequenos - animados se minerando) ===
        float arm_bob = g_player.is_mining ? std::sin(g_player.mine_anim * 20.0f + g_player.anim_frame * 4.0f) * 0.14f : breath * 0.35f;
        float arm_sep = 0.28f;
        
        // Braco esquerdo
        float la_x = px - perp_x * arm_sep;
        float la_z = pz - perp_z * arm_sep;
        render_cube_3d(la_x, py + 0.25f + bob - arm_bob, la_z, 0.15f, suit_r * 0.96f, suit_g * 0.96f, suit_b, 1.0f, true);
        
        // Braco direito
        float ra_x = px + perp_x * arm_sep;
        float ra_z = pz + perp_z * arm_sep;
        render_cube_3d(ra_x, py + 0.25f + bob + arm_bob + mine_impact * 0.05f, ra_z, 0.15f, suit_r * 0.96f, suit_g * 0.96f, suit_b, 1.0f, true);

    }

    if (g_debug && DEBUG_DRAW_COLLISIONS) {
        render_physics_debug_3d();
    }

    // === SELECAO DO ALVO / PLACE (Estilo Minicraft) ===
    // Desenha um contorno no bloco/tile sob a mira para deixar claro o que sera minerado/coletado/colocado.
    auto draw_tile_outline = [&](int tx, int tz, float y, float size, float r, float g, float b, float a, float lw) {
        float half = size * 0.5f;
        glLineWidth(lw);
        glColor4f(r, g, b, a);
        glBegin(GL_LINE_LOOP);
        glVertex3f((float)tx - half, y, (float)tz - half);
        glVertex3f((float)tx + half, y, (float)tz - half);
        glVertex3f((float)tx + half, y, (float)tz + half);
        glVertex3f((float)tx - half, y, (float)tz + half);
        glEnd();
    };

    int ptx = world_to_tile(rpos.x);
    int ptz = world_to_tile(rpos.y);
    bool draw_selection_boxes = (g_debug && DEBUG_DRAW_COLLISIONS);
    auto is_player_tile = [&](int tx, int tz) -> bool {
        return tx == ptx && tz == ptz;
    };

    if (draw_selection_boxes && g_has_target && g_world->in_bounds(g_target_x, g_target_y)) {
        Block tb = g_world->get(g_target_x, g_target_y);
        float base_y = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale;

        glDisable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Evita caixa em volta do proprio jogador.
        if (!is_player_tile(g_target_x, g_target_y)) {
            // Contorno preto + contorno branco por cima (boa leitura em qualquer tile)
            if (tb == Block::Air || tb == Block::Leaves || tb == Block::Water || is_ground_like(tb)) {
                float y = base_y + 0.018f;
                if (tb == Block::Leaves) y = base_y + 0.60f + 0.004f;
                else if (tb == Block::Water) y = base_y - 0.18f + 0.004f;
                draw_tile_outline(g_target_x, g_target_y, y, 1.03f, 0.0f, 0.0f, 0.0f, 0.85f, 2.5f);
                draw_tile_outline(g_target_x, g_target_y, y, 1.03f, 1.0f, 1.0f, 1.0f, 0.80f, 1.5f);
            } else {
                float cy = base_y + 0.5f;
                render_cube_outline_3d((float)g_target_x, cy, (float)g_target_y, 1.04f, 2.5f);

                // Outline branco leve
                glLineWidth(1.5f);
                glColor4f(1.0f, 1.0f, 1.0f, 0.55f);
                float half = 1.04f * 0.5f;
                glBegin(GL_LINE_LOOP);
                glVertex3f((float)g_target_x - half, cy + half, (float)g_target_y - half);
                glVertex3f((float)g_target_x + half, cy + half, (float)g_target_y - half);
                glVertex3f((float)g_target_x + half, cy + half, (float)g_target_y + half);
                glVertex3f((float)g_target_x - half, cy + half, (float)g_target_y + half);
                glEnd();
            }
        }
    }

    if (draw_selection_boxes && g_has_place_target && g_world->in_bounds(g_place_x, g_place_y) && !is_player_tile(g_place_x, g_place_y)) {
        // Mostra um contorno azul para o tile onde o RMB vai colocar
        Block pb = g_world->get(g_place_x, g_place_y);
        float base_y = (float)g_world->height_at(g_place_x, g_place_y) * kHeightScale;
        float y = base_y + 0.020f;
        if (pb == Block::Leaves) y = base_y + 0.60f + 0.004f;
        else if (pb == Block::Water) y = base_y - 0.18f + 0.004f;
        draw_tile_outline(g_place_x, g_place_y, y, 1.05f, 0.05f, 0.65f, 1.0f, 0.65f, 2.0f);
    }

    // === EFEITO DE MINERACAO (cracks) - SEM WIREFRAME ===
    if (g_has_target) {
        float target_x = (float)g_target_x;
        float target_z = (float)g_target_y;
        Block tb = g_world->get(g_target_x, g_target_y);
        float base_y = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale;

        // Overlay de "cracks" durante mineracao (progresso)
        if (g_tex_atlas != 0 && g_mine_progress > 0.001f &&
            g_mine_block_x == g_target_x && g_mine_block_y == g_target_y) {
            int lvl = std::clamp((int)std::floor(g_mine_progress * 8.0f), 0, 7);
            Tile crack = (Tile)((int)Tile::Crack1 + lvl);

            float crack_y = base_y + 0.01f + 0.002f;
            if (tb == Block::Leaves) crack_y = base_y + 0.60f + 0.002f;
            else if (tb == Block::Water) crack_y = base_y - 0.18f + 0.002f;
            else if (tb != Block::Air && !is_ground_like(tb)) crack_y = base_y + get_block_height(tb) + 0.002f;

            glDepthMask(GL_FALSE);
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, g_tex_atlas);
            render_plane_3d_tex(target_x, crack_y, target_z, 1.04f, crack, 1.0f, 1.0f, 1.0f, 1.0f);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
            glDepthMask(GL_TRUE);

            // Barra de quebra opcional (feedback claro).
            if (!is_player_tile(g_target_x, g_target_y)) {
                float bar_w = 0.84f;
                float bar_h = 0.06f;
                float y = crack_y + 0.012f;
                float x0 = (float)g_target_x - bar_w * 0.5f;
                float z0 = (float)g_target_y - 0.56f;
                glDisable(GL_TEXTURE_2D);
                glColor4f(0.02f, 0.02f, 0.03f, 0.78f);
                glBegin(GL_QUADS);
                glVertex3f(x0, y, z0);
                glVertex3f(x0 + bar_w, y, z0);
                glVertex3f(x0 + bar_w, y, z0 + bar_h);
                glVertex3f(x0, y, z0 + bar_h);
                glEnd();

                float fill = std::clamp(g_mine_progress, 0.0f, 1.0f) * (bar_w - 0.02f);
                glColor4f(0.95f, 0.82f, 0.26f, 0.92f);
                glBegin(GL_QUADS);
                glVertex3f(x0 + 0.01f, y + 0.001f, z0 + 0.01f);
                glVertex3f(x0 + 0.01f + fill, y + 0.001f, z0 + 0.01f);
                glVertex3f(x0 + 0.01f + fill, y + 0.001f, z0 + bar_h - 0.01f);
                glVertex3f(x0 + 0.01f, y + 0.001f, z0 + bar_h - 0.01f);
                glEnd();
            }
        }
    }
    
    // === BEACON VISUAL DA BASE (farol visivel a distancia) ===
    {
        float bx = (float)g_base_x + 0.5f;
        float bz = (float)g_base_y + 0.5f;
        float base_h = (float)g_world->height_at(g_base_x, g_base_y) * kHeightScale;
        float beacon_h = base_h + g_base_cfg.beacon_height;
        
        // Verificar se esta longe da base para mostrar o beacon
        float dx_beacon = g_player.pos.x - (float)g_base_x;
        float dy_beacon = g_player.pos.y - (float)g_base_y;
        float dist_beacon = std::sqrt(dx_beacon * dx_beacon + dy_beacon * dy_beacon);
        
        // Mostrar beacon quando longe da base (mais intenso quanto mais longe)
        float beacon_intensity = std::clamp((dist_beacon - g_base_cfg.safe_radius) / 50.0f, 0.0f, 1.0f);
        
        if (beacon_intensity > 0.01f) {
            // Pulso animado
            float pulse = 0.5f + 0.5f * std::sin(g_day_time * g_base_cfg.beacon_pulse_speed);
            float final_alpha = beacon_intensity * pulse * g_base_cfg.beacon_alpha;
            
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDisable(GL_TEXTURE_2D);
            glDepthMask(GL_FALSE);
            
            // Pilar de luz principal (gradiente vertical)
            glBegin(GL_QUAD_STRIP);
            for (int i = 0; i <= 20; ++i) {
                float t = (float)i / 20.0f;
                float y = base_h + t * (beacon_h - base_h);
                float alpha = (1.0f - t * 0.8f) * final_alpha;
                float width = 0.2f * (1.0f - t * 0.5f);  // Afina no topo
                
                // Cor azul ciano pulsante
                glColor4f(0.3f, 0.8f, 1.0f, alpha);
                glVertex3f(bx - width, y, bz);
                glVertex3f(bx + width, y, bz);
            }
            glEnd();
            
            // Pilar secundario (perpendicular para visibilidade 3D)
            glBegin(GL_QUAD_STRIP);
            for (int i = 0; i <= 20; ++i) {
                float t = (float)i / 20.0f;
                float y = base_h + t * (beacon_h - base_h);
                float alpha = (1.0f - t * 0.8f) * final_alpha * 0.7f;
                float width = 0.15f * (1.0f - t * 0.5f);
                
                glColor4f(0.3f, 0.8f, 1.0f, alpha);
                glVertex3f(bx, y, bz - width);
                glVertex3f(bx, y, bz + width);
            }
            glEnd();
            
            // Halo na base do beacon
            float halo_pulse = 0.6f + 0.4f * pulse;
            glBegin(GL_TRIANGLE_FAN);
            glColor4f(0.3f, 0.8f, 1.0f, final_alpha * 0.5f * halo_pulse);
            glVertex3f(bx, base_h + 0.1f, bz);
            glColor4f(0.3f, 0.8f, 1.0f, 0.0f);
            for (int i = 0; i <= 16; ++i) {
                float angle = (float)i * (2.0f * kPi / 16.0f);
                float halo_r = 1.5f * halo_pulse;
                glVertex3f(bx + std::cos(angle) * halo_r, base_h + 0.05f, bz + std::sin(angle) * halo_r);
            }
            glEnd();
            
            glDepthMask(GL_TRUE);
        }
    }
    
    // === MUDAR PARA PROJECAO 2D PARA HUD ===
    glDisable(GL_FOG);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_w, win_h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    
    // === VINHETA (RTX FAKE - efeito cinematico) ===
    if (g_lighting.enabled && g_lighting.vignette_intensity > 0.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        // Desenhar vinheta como gradiente radial usando quads
        float cx = win_w * 0.5f;
        float cy = win_h * 0.5f;
        float max_dist = std::sqrt(cx * cx + cy * cy);
        float vignette_start = g_lighting.vignette_radius * max_dist;
        
        // Criar overlay de vinheta com gradiente
        int segments = 32;
        for (int ring = 0; ring < 8; ++ring) {
            float inner_r = vignette_start + ring * (max_dist - vignette_start) / 8.0f;
            float outer_r = vignette_start + (ring + 1) * (max_dist - vignette_start) / 8.0f;
            float inner_alpha = (float)ring / 8.0f * g_lighting.vignette_intensity;
            float outer_alpha = (float)(ring + 1) / 8.0f * g_lighting.vignette_intensity;
            
            glBegin(GL_QUAD_STRIP);
            for (int i = 0; i <= segments; ++i) {
                float angle = (float)i / segments * 2.0f * kPi;
                float cos_a = std::cos(angle);
                float sin_a = std::sin(angle);
                
                glColor4f(0.0f, 0.0f, 0.0f, outer_alpha);
                glVertex2f(cx + outer_r * cos_a, cy + outer_r * sin_a);
                glColor4f(0.0f, 0.0f, 0.0f, inner_alpha);
                glVertex2f(cx + inner_r * cos_a, cy + inner_r * sin_a);
            }
            glEnd();
        }
    }
    
    // === DEBUG: VISUALIZAR LIGHTMAP ===
    if (g_debug_lightmap && g_lighting.enabled) {
        float debug_size = 150.0f;
        float debug_x = win_w - debug_size - 10.0f;
        float debug_y = 10.0f;
        float cell_size = debug_size / kLightmapSize;
        
        // Fundo
        glColor4f(0.0f, 0.0f, 0.0f, 0.8f);
        glBegin(GL_QUADS);
        glVertex2f(debug_x - 5, debug_y - 5);
        glVertex2f(debug_x + debug_size + 5, debug_y - 5);
        glVertex2f(debug_x + debug_size + 5, debug_y + debug_size + 5);
        glVertex2f(debug_x - 5, debug_y + debug_size + 5);
        glEnd();
        
        // Lightmap pixels
        for (int z = 0; z < kLightmapSize; ++z) {
            for (int x = 0; x < kLightmapSize; ++x) {
                int idx = z * kLightmapSize + x;
                float r = std::min(1.0f, g_lightmap_r[idx]);
                float g = std::min(1.0f, g_lightmap_g[idx]);
                float b = std::min(1.0f, g_lightmap_b[idx]);
                
                glColor3f(r, g, b);
                float px = debug_x + x * cell_size;
                float py = debug_y + z * cell_size;
                glBegin(GL_QUADS);
                glVertex2f(px, py);
                glVertex2f(px + cell_size, py);
                glVertex2f(px + cell_size, py + cell_size);
                glVertex2f(px, py + cell_size);
                glEnd();
            }
        }
        
        // Label
        draw_text(debug_x, debug_y + debug_size + 10.0f, "LIGHTMAP DEBUG", 0.9f, 0.9f, 0.3f, 1.0f);
    }
    
    // === DEBUG: VISUALIZAR LUZES ===
    if (g_debug_lights && g_lighting.enabled) {
        float debug_y = g_debug_lightmap ? 180.0f : 10.0f;
        char buf[128];
        snprintf(buf, sizeof(buf), "Luzes ativas: %d", (int)g_lights.size());
        draw_text(win_w - 200.0f, debug_y, buf, 0.9f, 0.9f, 0.3f, 1.0f);
        
        float y_offset = debug_y + 20.0f;
        for (size_t i = 0; i < std::min(g_lights.size(), (size_t)8); ++i) {
            const auto& light = g_lights[i];
            snprintf(buf, sizeof(buf), "L%d: (%.1f,%.1f) r=%.1f i=%.2f", 
                (int)i, light.x, light.y, light.radius, light.intensity);
            draw_text(win_w - 200.0f, y_offset, buf, light.r, light.g, light.b, 1.0f);
            y_offset += 15.0f;
        }
    }
    
    // === CROSSHAIR SEGUINDO O MOUSE (Estilo Minicraft) ===
    {
        float cx = (float)g_mouse_x;
        float cy = (float)g_mouse_y;
        float cross_size = 12.0f;
        float cross_thick = 2.0f;
        
        // Contorno preto
        glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
        glLineWidth(cross_thick + 2.0f);
        glBegin(GL_LINES);
        glVertex2f(cx - cross_size, cy);
        glVertex2f(cx + cross_size, cy);
        glVertex2f(cx, cy - cross_size);
        glVertex2f(cx, cy + cross_size);
        glEnd();
        
        // Crosshair branco
        glColor4f(1.0f, 1.0f, 1.0f, 0.9f);
        glLineWidth(cross_thick);
        glBegin(GL_LINES);
        glVertex2f(cx - cross_size, cy);
        glVertex2f(cx + cross_size, cy);
        glVertex2f(cx, cy - cross_size);
        glVertex2f(cx, cy + cross_size);
        glEnd();
        
        // Ponto central
        glPointSize(4.0f);
        glBegin(GL_POINTS);
        glVertex2f(cx, cy);
        glEnd();
    }

    // HUD
    if (g_state == GameState::Playing || g_state == GameState::Paused) {
        
        // ============= BARRA DE PROGRESSO DE TERRAFORMACAO (TOPO) =============
        {
            float progress_w = 400.0f;
            float progress_h = 22.0f;
            float progress_x = win_w * 0.5f - progress_w * 0.5f;
            float progress_y = 12.0f;
            
            // Fundo da barra
            render_quad(progress_x - 4.0f, progress_y - 4.0f, progress_w + 8.0f, progress_h + 8.0f, 
                0.0f, 0.0f, 0.0f, 0.65f);
            
            // Barra de progresso colorida por fase
            float pct = g_terraform / 100.0f;
            float pr, pg, pb;
            std::string phase_name;
            if (g_phase == TerraPhase::Frozen) { pr = 0.4f; pg = 0.6f; pb = 0.9f; phase_name = "Congelado"; }
            else if (g_phase == TerraPhase::Warming) { pr = 0.9f; pg = 0.6f; pb = 0.3f; phase_name = "Aquecendo"; }
            else if (g_phase == TerraPhase::Thawing) { pr = 0.4f; pg = 0.8f; pb = 0.9f; phase_name = "Degelo"; }
            else if (g_phase == TerraPhase::Habitable) { pr = 0.3f; pg = 0.9f; pb = 0.4f; phase_name = "Habitavel"; }
            else { pr = 0.2f; pg = 1.0f; pb = 0.5f; phase_name = "Terraformado"; }
            
            // Barra de fundo (cinza)
            render_quad(progress_x, progress_y, progress_w, progress_h, 0.15f, 0.15f, 0.18f, 0.90f);
            
            // Barra de progresso
            render_quad(progress_x, progress_y, progress_w * pct, progress_h, pr, pg, pb, 0.95f);
            
            // Bordas pixeladas
            render_quad(progress_x, progress_y, progress_w, 2.0f, 0.4f, 0.4f, 0.45f, 0.90f);
            render_quad(progress_x, progress_y + progress_h - 2.0f, progress_w, 2.0f, 0.1f, 0.1f, 0.12f, 0.90f);
            
            // Texto de progresso
            char buf[64];
            snprintf(buf, sizeof(buf), "%d%% - %s", (int)(pct * 100.0f), phase_name.c_str());
            float tw = estimate_text_w_px(buf);
            draw_text(progress_x + progress_w * 0.5f - tw * 0.5f, progress_y + 15.0f, buf, 
                kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.95f);
        }
        
        float x0 = 20.0f;
        float y0 = 50.0f;  // Ajustado para dar espaco para a barra de terraformacao
        float bar_w = 180.0f;
        float bar_h = 14.0f;
        float bar_gap = 18.0f;
        
        // Check if player is at base (usando distancia 2D)
        float dx_base = g_player.pos.x - (float)g_base_x;
        float dy_base = g_player.pos.y - (float)g_base_y;
        float dist_to_base = std::sqrt(dx_base * dx_base + dy_base * dy_base);
        bool at_base = (dist_to_base < g_base_cfg.safe_radius);
        
        // === FUNDO TRANSPARENTE DO HUD ESQUERDO ===
        float left_panel_h = bar_gap * 10 + 100.0f;  // Altura aproximada do painel esquerdo (incluindo jetpack)
        render_quad(x0 - 10.0f, y0 - 10.0f, bar_w + 20.0f, left_panel_h, 0.0f, 0.0f, 0.0f, 0.30f);
        
        // === LEFT PANEL: SUIT STATUS (Player) ===
        draw_text(x0, y0 - 2.0f, "TRAJE", 0.70f, 0.75f, 0.85f, 0.85f);
        y0 += 12.0f;
        
        // HP Bar (vermelho - usando cores centralizadas)
        float hp_pct = g_player.hp / 100.0f;
        bool hp_crit = hp_pct < 0.25f;
        float hp_flash = hp_crit ? (0.7f + 0.3f * std::sin(g_player.anim_frame * 6.0f)) : 1.0f;
        render_bar(x0, y0, bar_w, 16.0f, hp_pct, kColorHp[0] * hp_flash, kColorHp[1], kColorHp[2]);
        draw_text(x0 + 6.0f, y0 + 12.0f, "HP " + std::to_string(g_player.hp), 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.95f);
        
        // Suit Oxygen (verde - usando cores centralizadas)
        float o2_pct = g_player_oxygen / 100.0f;
        bool o2_crit = o2_pct < 0.25f;
        float o2_r = o2_crit ? kColorDanger[0] : kColorOxygen[0];
        float o2_g = o2_crit ? kColorDanger[1] : kColorOxygen[1];
        float o2_b = o2_crit ? kColorDanger[2] : kColorOxygen[2];
        float o2_flash = o2_crit ? (0.7f + 0.3f * std::sin(g_player.anim_frame * 6.0f)) : 1.0f;
        render_bar(x0, y0 + bar_gap, bar_w, bar_h, o2_pct, o2_r * o2_flash, o2_g * o2_flash, o2_b);
        draw_text(x0 + 6.0f, y0 + bar_gap + 11.0f, "O2 " + std::to_string((int)g_player_oxygen) + "%", 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // Suit Water (azul - usando cores centralizadas)
        float water_pct = g_player_water / 100.0f;
        bool water_crit = water_pct < 0.25f;
        float water_flash = water_crit ? (0.7f + 0.3f * std::sin(g_player.anim_frame * 6.0f)) : 1.0f;
        render_bar(x0, y0 + bar_gap * 2, bar_w, bar_h, water_pct, 
            (water_crit ? kColorDanger[0] : kColorWater[0]) * water_flash,
            (water_crit ? kColorDanger[1] : kColorWater[1]) * water_flash,
            water_crit ? kColorDanger[2] : kColorWater[2]);
        draw_text(x0 + 6.0f, y0 + bar_gap * 2 + 11.0f, "H2O " + std::to_string((int)g_player_water) + "%", 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // Suit Food (laranja - usando cores centralizadas)
        float food_pct = g_player_food / 100.0f;
        bool food_crit = food_pct < 0.25f;
        render_bar(x0, y0 + bar_gap * 3, bar_w, bar_h, food_pct, 
            food_crit ? kColorWarning[0] : kColorFood[0],
            food_crit ? kColorWarning[1] : kColorFood[1],
            food_crit ? kColorWarning[2] : kColorFood[2]);
        draw_text(x0 + 6.0f, y0 + bar_gap * 3 + 11.0f, "Comida " + std::to_string((int)g_player_food) + "%", 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // Jetpack Fuel (amarelo-laranja)
        float jet_pct = g_player.jetpack_fuel / 100.0f;
        bool jet_active = g_player.jetpack_active;
        float jet_r = jet_active ? 1.0f : 0.85f;
        float jet_g = jet_active ? 0.65f : 0.55f;
        float jet_b = 0.15f;
        float jet_pulse = jet_active ? (0.8f + 0.2f * std::sin(g_player.jetpack_flame_anim)) : 1.0f;
        render_bar(x0, y0 + bar_gap * 4, bar_w, bar_h, jet_pct, 
            jet_r * jet_pulse, jet_g * jet_pulse, jet_b);
        std::string jet_label = jet_active ? "JETPACK ATIVO" : "Jetpack " + std::to_string((int)g_player.jetpack_fuel) + "%";
        draw_text(x0 + 6.0f, y0 + bar_gap * 4 + 11.0f, jet_label, 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], 0.90f);
        
        // === LEFT PANEL: BASE STATUS ===
        y0 += bar_gap * 5 + 15.0f;
        
        // At base indicator
        if (at_base) {
            render_quad(x0 - 5.0f, y0 - 5.0f, bar_w + 10.0f, 20.0f, 0.15f, 0.35f, 0.20f, 0.80f);
            draw_text(x0, y0 + 10.0f, "NA BASE - RECARREGANDO", 0.40f, 0.95f, 0.50f, 0.95f);
            y0 += 22.0f;
        } else {
            draw_text(x0, y0 + 10.0f, "ARMAZENAMENTO DA BASE", 0.70f, 0.75f, 0.85f, 0.85f);
            y0 += 15.0f;
        }
        
        render_bar(x0, y0, bar_w, bar_h, g_base_energy / kBaseEnergyMax, 0.95f, 0.84f, 0.25f);
        draw_text(x0 + 6.0f, y0 + 11.0f, "Energia " + std::to_string((int)g_base_energy) + "/" + std::to_string((int)kBaseEnergyMax), 0.90f, 0.90f, 0.90f, 0.90f);

        render_bar(x0, y0 + bar_gap, bar_w, bar_h, g_base_water / kBaseWaterMax, 0.25f, 0.65f, 0.95f);
        draw_text(x0 + 6.0f, y0 + bar_gap + 11.0f, "Agua " + std::to_string((int)g_base_water) + "/" + std::to_string((int)kBaseWaterMax), 0.90f, 0.90f, 0.90f, 0.90f);

        render_bar(x0, y0 + bar_gap * 2, bar_w, bar_h, g_base_oxygen / kBaseOxygenMax, 0.20f, 0.95f, 0.55f);
        draw_text(x0 + 6.0f, y0 + bar_gap * 2 + 11.0f, "Oxigenio " + std::to_string((int)g_base_oxygen) + "/" + std::to_string((int)kBaseOxygenMax), 0.90f, 0.90f, 0.90f, 0.90f);
        
        render_bar(x0, y0 + bar_gap * 3, bar_w, bar_h, g_base_food / kBaseFoodMax, 0.85f, 0.65f, 0.25f);
        draw_text(x0 + 6.0f, y0 + bar_gap * 3 + 11.0f, "Comida " + std::to_string((int)g_base_food) + "/" + std::to_string((int)kBaseFoodMax), 0.90f, 0.90f, 0.90f, 0.90f);
        
        // Integrity bar with color based on level
        float int_r = g_base_integrity > 50.0f ? 0.35f : (g_base_integrity > 25.0f ? 0.90f : 0.95f);
        float int_g = g_base_integrity > 50.0f ? 0.85f : (g_base_integrity > 25.0f ? 0.65f : 0.25f);
        float int_b = g_base_integrity > 50.0f ? 0.45f : 0.20f;
        render_bar(x0, y0 + bar_gap * 4, bar_w, bar_h, g_base_integrity / kBaseIntegrityMax, int_r, int_g, int_b);
        draw_text(x0 + 6.0f, y0 + bar_gap * 4 + 11.0f, "Integ " + std::to_string((int)g_base_integrity) + "/" + std::to_string((int)kBaseIntegrityMax), 0.90f, 0.90f, 0.90f, 0.90f);
        
        // === RIGHT PANEL: Terraforming Stats ===
        float rx0 = win_w - bar_w - 30.0f;
        float ry0 = 18.0f;
        
        // === FUNDO TRANSPARENTE DO HUD DIREITO ===
        float right_panel_h = bar_gap * 6 + 90.0f;  // Altura aproximada do painel direito
        render_quad(rx0 - 10.0f, ry0 - 10.0f, bar_w + 20.0f, right_panel_h, 0.0f, 0.0f, 0.0f, 0.30f);
        
        // Phase indicator
        float phase_colors[5][3] = {
            {0.4f, 0.6f, 0.9f},  // Frozen - blue
            {0.9f, 0.6f, 0.3f},  // Warming - orange
            {0.4f, 0.8f, 0.9f},  // Thawing - cyan
            {0.3f, 0.9f, 0.4f},  // Habitable - green
            {0.2f, 1.0f, 0.5f},  // Terraformed - bright green
        };
        int pi = (int)g_phase;
        render_quad(rx0, ry0, bar_w, 20.0f, phase_colors[pi][0] * 0.3f, phase_colors[pi][1] * 0.3f, phase_colors[pi][2] * 0.3f, 0.7f);
        draw_text(rx0 + 6.0f, ry0 + 15.0f, std::string("Fase: ") + phase_name(g_phase), phase_colors[pi][0], phase_colors[pi][1], phase_colors[pi][2], 0.98f);
        
        // Temperature
        ry0 += 28.0f;
        float temp_pct = clamp01((g_temperature + 60.0f) / 100.0f); // -60 to +40
        float temp_r = temp_pct;
        float temp_b = 1.0f - temp_pct;
        render_bar(rx0, ry0, bar_w, bar_h, temp_pct, temp_r, 0.3f, temp_b);
        char temp_str[32];
        snprintf(temp_str, sizeof(temp_str), "Temp %.0fC", g_temperature);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, temp_str, 0.95f, 0.95f, 0.95f, 0.90f);
        
        // CO2 Level
        ry0 += bar_gap;
        render_bar(rx0, ry0, bar_w, bar_h, g_co2_level / 100.0f, 0.70f, 0.50f, 0.30f);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, "CO2 " + std::to_string((int)g_co2_level) + "%", 0.90f, 0.90f, 0.90f, 0.90f);
        
        // Atmosphere
        ry0 += bar_gap;
        render_bar(rx0, ry0, bar_w, bar_h, g_atmosphere / 100.0f, 0.50f, 0.70f, 0.90f);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, "Atmos " + std::to_string((int)g_atmosphere) + "%", 0.90f, 0.90f, 0.90f, 0.90f);
        
        // Terraform Progress
        ry0 += bar_gap;
        render_bar(rx0, ry0, bar_w, bar_h, g_terraform / 100.0f, 0.25f, 0.90f, 0.40f);
        draw_text(rx0 + 6.0f, ry0 + 11.0f, "Terraform " + std::to_string((int)g_terraform) + "%", 0.90f, 0.90f, 0.90f, 0.90f);
        
        // === BASE INDICATOR (com direcao 2D e seta) ===
        ry0 += bar_gap + 10.0f;
        {
            float dir_x = (float)g_base_x - g_player.pos.x;
            float dir_y = (float)g_base_y - g_player.pos.y;
            float dist_blocks = std::sqrt(dir_x * dir_x + dir_y * dir_y);
            
            // Fundo do indicador
            render_quad(rx0, ry0, bar_w, 28.0f, 0.15f, 0.18f, 0.25f, 0.75f);
            
            // Cores baseadas na distancia
            float dist_alpha = (dist_blocks > 30.0f) ? 0.95f : 0.70f;
            float dist_r = (dist_blocks > 80.0f) ? 0.95f : (at_base ? 0.35f : 0.65f);
            float dist_g = (dist_blocks > 80.0f) ? 0.55f : (at_base ? 0.85f : 0.85f);
            float dist_b = at_base ? 0.45f : 0.60f;
            
            // Texto de distancia
            char dist_str[64];
            if (at_base) {
                snprintf(dist_str, sizeof(dist_str), "BASE (Zona Segura)");
            } else {
                snprintf(dist_str, sizeof(dist_str), "Base: %.0fm", dist_blocks);
            }
            draw_text(rx0 + 6.0f, ry0 + 12.0f, dist_str, dist_r, dist_g, dist_b, dist_alpha);
            
            // Desenhar seta de direcao quando longe da base
            if (!at_base && dist_blocks > 5.0f) {
                float arrow_cx = rx0 + bar_w - 35.0f;
                float arrow_cy = ry0 + 14.0f;
                float angle = std::atan2(dir_y, dir_x);
                float arrow_size = 10.0f;
                
                // Normalizar direcao
                float nx = dir_x / dist_blocks;
                float ny = dir_y / dist_blocks;
                
                // Ponta da seta
                float tip_x = arrow_cx + nx * arrow_size;
                float tip_y = arrow_cy + ny * arrow_size;
                
                // Base da seta (perpendicular)
                float perp_x = -ny * arrow_size * 0.5f;
                float perp_y = nx * arrow_size * 0.5f;
                
                // Cor pulsante para seta
                float pulse = 0.7f + 0.3f * std::sin(g_day_time * 3.0f);
                
                glBegin(GL_TRIANGLES);
                glColor4f(0.3f * pulse, 0.8f * pulse, 1.0f * pulse, 0.9f);
                glVertex2f(tip_x, tip_y);
                glVertex2f(arrow_cx - nx * arrow_size * 0.3f + perp_x, arrow_cy - ny * arrow_size * 0.3f + perp_y);
                glVertex2f(arrow_cx - nx * arrow_size * 0.3f - perp_x, arrow_cy - ny * arrow_size * 0.3f - perp_y);
                glEnd();
            }
            
            // Tecla de atalho
            draw_text(rx0 + bar_w - 22.0f, ry0 + 24.0f, "[H]", 0.55f, 0.75f, 0.95f, 0.70f);
        }
        
        // === MINIMAPA ===
        if (!g_minimap.world_map_open) {
            render_minimap(win_w, win_h);
        }

        // === HOTBAR ESTILO MINICRAFT ===
        // Funcao local para desenhar slot pixelado
        auto draw_minicraft_slot = [&](float x, float y, float size, bool selected, Block block, int key_num, int count) {
            // Fundo escuro
            render_quad(x, y, size, size, 0.15f, 0.15f, 0.18f, 0.92f);
            
            // Borda pixelada (3 pixels)
            float border = 3.0f;
            // Borda clara superior/esquerda
            render_quad(x, y, size, border, 0.45f, 0.45f, 0.50f, 0.95f);
            render_quad(x, y, border, size, 0.45f, 0.45f, 0.50f, 0.95f);
            // Borda escura inferior/direita
            render_quad(x, y + size - border, size, border, 0.08f, 0.08f, 0.10f, 0.95f);
            render_quad(x + size - border, y, border, size, 0.08f, 0.08f, 0.10f, 0.95f);
            
            // Highlight se selecionado
            if (selected) {
                render_quad(x - 3.0f, y - 3.0f, size + 6.0f, size + 6.0f, 0.95f, 0.95f, 0.35f, 0.35f);
                render_quad(x + 2.0f, y + 2.0f, size - 4.0f, size - 4.0f, 0.25f, 0.25f, 0.30f, 0.90f);
            }
            
            // Icone do bloco (cubo 3D simples)
            float icon_size = size * 0.55f;
            float ix = x + (size - icon_size) * 0.5f + 2.0f;
            float iy = y + (size - icon_size) * 0.4f;
            if (g_tex_atlas != 0) {
                BlockTex bt = block_tex(block);
                int wf = ((int)std::floor(g_day_time * 4.0f)) & 3;
                if (bt.is_water) {
                    bt.top = (Tile)((int)Tile::Water0 + wf);
                    bt.side = bt.top;
                    bt.bottom = bt.top;
                }
                float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, alpha = 1.0f;
                if (bt.uses_tint || bt.transparent) {
                    float cr, cg, cb, ca;
                    block_color(block, 128, 256, cr, cg, cb, ca);
                    if (bt.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                    if (bt.transparent) alpha = ca;
                }

                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, g_tex_atlas);
                render_quad_tex(ix, iy, icon_size, icon_size * 0.5f, bt.top, tint_r, tint_g, tint_b, 0.98f * alpha);
                render_quad_tex(ix, iy + icon_size * 0.5f, icon_size, icon_size * 0.5f, bt.side,
                                tint_r * 0.75f, tint_g * 0.75f, tint_b * 0.75f, 0.98f * alpha);
                glBindTexture(GL_TEXTURE_2D, 0);
                glDisable(GL_TEXTURE_2D);

                // Linha de divisao
                glLineWidth(1.0f);
                glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
                glBegin(GL_LINES);
                glVertex2f(ix, iy + icon_size * 0.5f);
                glVertex2f(ix + icon_size, iy + icon_size * 0.5f);
                glEnd();
            } else {
                float r, g, bl, a;
                block_color(block, 128, 256, r, g, bl, a);
                // Face superior
                render_quad(ix, iy, icon_size, icon_size * 0.5f, r, g, bl, 0.98f);
                // Face frontal (mais escura)
                render_quad(ix, iy + icon_size * 0.5f, icon_size, icon_size * 0.5f, r * 0.7f, g * 0.7f, bl * 0.7f, 0.98f);
                // Linha de divisao
                glLineWidth(1.0f);
                glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
                glBegin(GL_LINES);
                glVertex2f(ix, iy + icon_size * 0.5f);
                glVertex2f(ix + icon_size, iy + icon_size * 0.5f);
                glEnd();
            }
             
            // Numero da tecla (canto superior esquerdo)
            if (key_num >= 0) {
                draw_text(x + 4.0f, y + 12.0f, std::to_string(key_num), 0.95f, 0.95f, 0.95f, 0.90f);
            }
            
            // Quantidade (canto inferior direito)
            if (count >= 0) {
                std::string cnt = std::to_string(count);
                float tw = estimate_text_w_px(cnt);
                draw_text(x + size - tw - 5.0f, y + size - 5.0f, cnt, 0.95f, 0.95f, 0.95f, 0.95f);
            }
        };
        
        // Slots de recursos (1-6)
        const Block resource_slots[] = {Block::Dirt, Block::Stone, Block::Iron, Block::Copper, Block::Coal, Block::Wood};
        const int res_count = 6;
        
        // Slots de modulos (7+) - apenas desbloqueados
        std::vector<Block> module_slots;
        if (g_unlocks.solar_unlocked) module_slots.push_back(Block::SolarPanel);
        if (g_unlocks.water_extractor_unlocked) module_slots.push_back(Block::WaterExtractor);
        if (g_unlocks.o2_generator_unlocked) module_slots.push_back(Block::OxygenGenerator);
        if (g_unlocks.greenhouse_unlocked) module_slots.push_back(Block::Greenhouse);
        if (g_unlocks.co2_factory_unlocked) module_slots.push_back(Block::CO2Factory);
        if (g_unlocks.habitat_unlocked) module_slots.push_back(Block::Habitat);
        if (g_unlocks.terraformer_unlocked) module_slots.push_back(Block::TerraformerBeacon);
        
        float slot_size = 48.0f;
        float slot_gap = 4.0f;
        
        // === HOTBAR UNIFICADA (centrada na base da tela) ===
        int total_slots = res_count + (int)module_slots.size();
        float total_w = total_slots * slot_size + (total_slots - 1) * slot_gap;
        float hx = win_w * 0.5f - total_w * 0.5f;
        float hy = win_h - slot_size - 12.0f;
        
        // Fundo da hotbar (painel escuro)
        render_quad(hx - 8.0f, hy - 8.0f, total_w + 16.0f, slot_size + 16.0f, 0.08f, 0.08f, 0.10f, 0.75f);
        
        // Funcao auxiliar para verificar se mouse esta sobre um slot
        auto mouse_over_slot = [&](float sx, float sy, float ss) -> bool {
            return g_mouse_x >= sx && g_mouse_x <= sx + ss && 
                   g_mouse_y >= sy && g_mouse_y <= sy + ss;
        };
        
        // Desenhar slots de recursos
        for (int i = 0; i < res_count; ++i) {
            float bx = hx + i * (slot_size + slot_gap);
            
            // Detectar clique do mouse no slot
            if (g_mouse_left_clicked && mouse_over_slot(bx, hy, slot_size) && g_state == GameState::Playing) {
                g_selected = resource_slots[i];
                bounce_hotbar_slot(i);
                g_mouse_left_clicked = false;
            }
            
            bool sel = (g_selected == resource_slots[i]);
            // Highlight se mouse esta sobre o slot
            bool hovered = mouse_over_slot(bx, hy, slot_size);
            int count = std::max(0, g_inventory[(int)resource_slots[i]]);
            
            // Desenhar com efeito de hover
            if (hovered && !sel) {
                render_quad(bx - 2.0f, hy - 2.0f, slot_size + 4.0f, slot_size + 4.0f, 0.55f, 0.65f, 0.85f, 0.35f);
            }
            draw_minicraft_slot(bx, hy, slot_size, sel, resource_slots[i], i + 1, count);
        }
        
        // Separador visual entre recursos e modulos
        if (!module_slots.empty()) {
            float sep_x = hx + res_count * (slot_size + slot_gap) - slot_gap * 0.5f;
            render_quad(sep_x - 1.0f, hy + 4.0f, 2.0f, slot_size - 8.0f, 0.40f, 0.40f, 0.45f, 0.80f);
        }
        
        // Desenhar slots de modulos
        for (int i = 0; i < (int)module_slots.size(); ++i) {
            float bx = hx + (res_count + i) * (slot_size + slot_gap);
            
            // Detectar clique do mouse no slot de modulo
            if (g_mouse_left_clicked && mouse_over_slot(bx, hy, slot_size) && g_state == GameState::Playing) {
                g_selected = module_slots[i];
                bounce_hotbar_slot(res_count + i);
                g_mouse_left_clicked = false;
            }
            
            bool sel = (g_selected == module_slots[i]);
            bool hovered = mouse_over_slot(bx, hy, slot_size);
            CraftCost c = module_cost(module_slots[i]);
            bool can_build = can_afford(c);
            int key_num = -1;
            if (i < 4) key_num = (i < 3) ? (7 + i) : 0;
            
            // Desenhar com efeito de hover
            if (hovered && !sel) {
                render_quad(bx - 2.0f, hy - 2.0f, slot_size + 4.0f, slot_size + 4.0f, 0.55f, 0.65f, 0.85f, 0.35f);
            }
            draw_minicraft_slot(bx, hy, slot_size, sel, module_slots[i], key_num, can_build ? 1 : 0);
        }
        
        // Info do item selecionado (acima da hotbar)
        {
            std::string s = std::string(block_name(g_selected));
            if (is_module(g_selected)) {
                if (!is_unlocked(g_selected)) {
                    s += " [" + unlock_progress_string(g_selected) + "]";
                } else {
                    s += " - " + cost_string(module_cost(g_selected));
                }
            } else {
                s += " x" + std::to_string(std::max(0, g_inventory[(int)g_selected]));
            }
            float tw = estimate_text_w_px(s);
            // Fundo do texto
            render_quad(win_w * 0.5f - tw * 0.5f - 8.0f, hy - 26.0f, tw + 16.0f, 18.0f, 0.0f, 0.0f, 0.0f, 0.65f);
            draw_text(win_w * 0.5f - tw * 0.5f, hy - 12.0f, s, 0.95f, 0.95f, 0.95f, 0.95f);
        }

        // Popups de coleta (feedback acima da hotbar)
        if (!g_collect_popups.empty()) {
            float base_x = win_w * 0.5f;
            float base_y = hy - 42.0f;
            float line_h = 18.0f;

            int n = (int)g_collect_popups.size();
            int max_show = 6;
            int start = std::max(0, n - max_show);

            for (int idx = n - 1; idx >= start; --idx) {
                int stack = (n - 1) - idx;
                const CollectPopup& p = g_collect_popups[idx];

                float alpha = std::min(1.0f, p.life / 0.45f);
                float tw = estimate_text_w_px(p.text);

                bool draw_icon = (g_tex_atlas != 0 && p.item != Block::Air);
                float icon_sz = 16.0f;
                float pad_x = 10.0f;
                float gap = 6.0f;
                float box_w = tw + pad_x * 2.0f + (draw_icon ? (icon_sz + gap) : 0.0f);

                float px = base_x + p.x - box_w * 0.5f;
                float py = base_y + p.y - (float)stack * line_h;

                // Fundo + faixa colorida
                render_quad(px, py - 14.0f, box_w, 18.0f, 0.0f, 0.0f, 0.0f, 0.58f * alpha);
                render_quad(px, py - 14.0f, 3.0f, 18.0f, p.r, p.g, p.b, 0.85f * alpha);

                float tx = px + pad_x;
                if (draw_icon) {
                    BlockTex bt = block_tex(p.item);
                    int wf = ((int)std::floor(g_day_time * 4.0f)) & 3;
                    if (bt.is_water) {
                        bt.top = (Tile)((int)Tile::Water0 + wf);
                        bt.side = bt.top;
                        bt.bottom = bt.top;
                    }

                    float tint_r = 1.0f, tint_g = 1.0f, tint_b = 1.0f, icon_a = 1.0f;
                    if (bt.uses_tint || bt.transparent) {
                        float cr, cg, cb, ca;
                        block_color(p.item, 128, 256, cr, cg, cb, ca);
                        if (bt.uses_tint) { tint_r = cr; tint_g = cg; tint_b = cb; }
                        if (bt.transparent) icon_a = ca;
                    }

                    glEnable(GL_TEXTURE_2D);
                    glBindTexture(GL_TEXTURE_2D, g_tex_atlas);
                    render_quad_tex(tx, py - 12.0f, icon_sz, icon_sz, bt.top, tint_r, tint_g, tint_b, 0.98f * alpha * icon_a);
                    glBindTexture(GL_TEXTURE_2D, 0);
                    glDisable(GL_TEXTURE_2D);

                    tx += icon_sz + gap;
                }

                draw_text(tx, py, p.text, p.r, p.g, p.b, 0.95f * alpha);
            }
        }

        // Target info
        if (g_has_target) {
            Block b = g_world->get(g_target_x, g_target_y);
            if (b != Block::Air) {
                float rr = g_target_in_range ? 0.85f : 0.95f;
                float gg = g_target_in_range ? 0.95f : 0.35f;
                draw_text(20.0f, win_h - 100.0f, std::string("Alvo: ") + block_name(b), rr, gg, 0.25f, 0.95f);
            }
        }

        // Debug info (3D)
        if (g_debug) {
            char buf[256];
            snprintf(buf, sizeof(buf), "XZ: %.1f,%.1f  Y: %.2f  Chao: %.1f  %s  Mat: %s  VelXY: %.2f",
                g_player.pos.x, g_player.pos.y, g_player.pos_y, g_player.ground_height,
                g_player.on_ground ? "NO CHAO" : "NO AR",
                g_physics.terrain_name.c_str(),
                vec2_length(g_player.vel));
            draw_text(20.0f, win_h - 136.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);
             
            snprintf(buf, sizeof(buf), "VelY: %.2f  Normal:(%.2f, %.2f, %.2f)  Coy:%.2f Buf:%.2f  %s%s%s",
                g_player.vel_y,
                g_physics.ground_normal.x, g_physics.ground_normal.y, g_physics.ground_normal.z,
                g_physics.coyote_timer, g_physics.jump_buffer_timer,
                g_physics.sliding ? "SLIDE " : "",
                g_physics.stepped ? "STEP " : "",
                (g_physics.hit_x || g_physics.hit_z) ? "HIT" : "");
            draw_text(20.0f, win_h - 118.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);

            snprintf(buf, sizeof(buf), "Cam: yaw=%.0f pitch=%.0f dist=%.1f mode=%s(%s) occ=%.2f encl=%.2f",
                g_camera.yaw, g_camera.pitch, g_camera.distance,
                camera_mode_name(g_camera_mode), g_camera_mode_reason.c_str(),
                g_camera_obstruction, g_camera_enclosed);
            draw_text(20.0f, win_h - 100.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);

            snprintf(buf, sizeof(buf), "Phys: dt=%.4f alpha=%.2f cam_hide=%.2fs cam_rays=%d",
                g_physics_cfg.fixed_timestep, g_physics.alpha, g_camera_hidden_time, g_camera_debug_ray_count);
            draw_text(20.0f, win_h - 82.0f, buf, 0.85f, 0.85f, 0.90f, 0.95f);
        }
    }

    // Toast notifications
    if (g_toast_time > 0.0f && !g_toast.empty()) {
        float toast_alpha = std::min(1.0f, g_toast_time);
        float tw = estimate_text_w_px(g_toast);
        render_quad(win_w * 0.5f - tw * 0.5f - 10.0f, 50.0f, tw + 20.0f, 28.0f, 0.0f, 0.0f, 0.0f, 0.6f * toast_alpha);
        draw_text(win_w * 0.5f - tw * 0.5f, 70.0f, g_toast, 0.95f, 0.95f, 0.50f, toast_alpha);
    }
    
    // ============= FEEDBACK VISUAL APRIMORADO =============
    
    // Flash vermelho (erro/dano)
    if (g_screen_flash_red > 0.0f) {
        float alpha = g_screen_flash_red * 0.4f;
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 
            kColorDanger[0], kColorDanger[1], kColorDanger[2], alpha);
    }
    
    // Flash verde (sucesso)
    if (g_screen_flash_green > 0.0f) {
        float alpha = g_screen_flash_green * 0.35f;
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 
            kColorSuccess[0], kColorSuccess[1], kColorSuccess[2], alpha);
    }
    
    // Popup grande de desbloqueio (conquista)
    if (g_unlock_popup_timer > 0.0f) {
        float alpha = std::min(1.0f, g_unlock_popup_timer);
        float popup_w = 380.0f;
        float popup_h = 100.0f;
        float px = win_w * 0.5f - popup_w * 0.5f;
        float py = win_h * 0.25f;
        
        // Fundo com borda verde
        render_quad(px - 4.0f, py - 4.0f, popup_w + 8.0f, popup_h + 8.0f, 
            kColorSuccess[0], kColorSuccess[1], kColorSuccess[2], 0.9f * alpha);
        render_quad(px, py, popup_w, popup_h, 0.05f, 0.08f, 0.05f, 0.95f * alpha);
        
        // Titulo
        float tw = estimate_text_w_px(g_unlock_popup_text);
        draw_text(win_w * 0.5f - tw * 0.5f, py + 35.0f, g_unlock_popup_text, 
            kColorSuccess[0], kColorSuccess[1], kColorSuccess[2], alpha);
        
        // Subtitulo
        float sw = estimate_text_w_px(g_unlock_popup_subtitle);
        draw_text(win_w * 0.5f - sw * 0.5f, py + 65.0f, g_unlock_popup_subtitle, 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], alpha * 0.9f);
    }
    
    // Dica de onboarding
    if (g_onboarding.tip_timer > 0.0f && !g_onboarding.current_tip.empty()) {
        float alpha = std::min(1.0f, g_onboarding.tip_timer);
        float tw = estimate_text_w_px(g_onboarding.current_tip);
        float tip_y = win_h * 0.15f;
        
        // Fundo azul suave
        render_quad(win_w * 0.5f - tw * 0.5f - 15.0f, tip_y - 10.0f, tw + 30.0f, 35.0f, 
            kColorSelection[0] * 0.3f, kColorSelection[1] * 0.3f, kColorSelection[2] * 0.3f, 0.85f * alpha);
        render_quad(win_w * 0.5f - tw * 0.5f - 15.0f, tip_y - 10.0f, 4.0f, 35.0f, 
            kColorSelection[0], kColorSelection[1], kColorSelection[2], 0.95f * alpha);
        
        draw_text(win_w * 0.5f - tw * 0.5f, tip_y + 10.0f, g_onboarding.current_tip, 
            kColorTextPrimary[0], kColorTextPrimary[1], kColorTextPrimary[2], alpha);
    }

    // Overlays - Menus estilo Minecraft
    if (g_state == GameState::Paused || g_state == GameState::Menu) {
        // Fundo escurecido
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, g_state == GameState::Paused ? 0.55f : 0.70f);
        
        // Funcao para desenhar botao estilo Minecraft
        auto draw_mc_button = [&](float x, float y, float w, float h, const std::string& text, bool hovered, bool enabled = true) {
            // Cores base do botao Minecraft
            float bg_r = enabled ? 0.45f : 0.30f;
            float bg_g = enabled ? 0.45f : 0.30f;
            float bg_b = enabled ? 0.50f : 0.35f;
            
            if (hovered && enabled) {
                bg_r = 0.55f; bg_g = 0.65f; bg_b = 0.85f;  // Highlight azul
            }
            
            // Sombra (borda inferior/direita escura)
            render_quad(x + 3.0f, y + 3.0f, w, h, 0.05f, 0.05f, 0.08f, 0.95f);
            
            // Corpo do botao
            render_quad(x, y, w, h, bg_r * 0.7f, bg_g * 0.7f, bg_b * 0.7f, 0.98f);
            
            // Borda superior clara (3D effect)
            render_quad(x, y, w, 3.0f, bg_r * 1.3f, bg_g * 1.3f, bg_b * 1.3f, 0.95f);
            render_quad(x, y, 3.0f, h, bg_r * 1.3f, bg_g * 1.3f, bg_b * 1.3f, 0.95f);
            
            // Borda inferior escura
            render_quad(x, y + h - 3.0f, w, 3.0f, bg_r * 0.4f, bg_g * 0.4f, bg_b * 0.4f, 0.95f);
            render_quad(x + w - 3.0f, y, 3.0f, h, bg_r * 0.4f, bg_g * 0.4f, bg_b * 0.4f, 0.95f);
            
            // Interior do botao (gradiente sutil)
            render_quad(x + 3.0f, y + 3.0f, w - 6.0f, h - 6.0f, bg_r, bg_g, bg_b, 0.98f);
            
            // Texto centralizado
            float text_w = estimate_text_w_px(text);
            float text_r = enabled ? 1.0f : 0.55f;
            float text_g = enabled ? 1.0f : 0.55f;
            float text_b = enabled ? 1.0f : 0.55f;
            if (hovered && enabled) {
                text_r = 1.0f; text_g = 1.0f; text_b = 0.65f;  // Texto amarelado quando hover
            }
            draw_text(x + (w - text_w) * 0.5f, y + h * 0.5f + 5.0f, text, text_r, text_g, text_b, 1.0f);
        };
        
        // Verifica se mouse esta sobre um retangulo
        auto mouse_in_rect = [&](float x, float y, float w, float h) -> bool {
            return g_mouse_x >= x && g_mouse_x <= x + w && g_mouse_y >= y && g_mouse_y <= y + h;
        };
        
        if (g_state == GameState::Paused) {
            // === MENU DE PAUSA ESTILO MINECRAFT ===
            float btn_w = 280.0f;
            float btn_h = 40.0f;
            float btn_gap = 8.0f;
            float start_y = win_h * 0.22f;
            float center_x = win_w * 0.5f - btn_w * 0.5f;
            
            // Titulo "Jogo Pausado"
            std::string title = "Jogo Pausado";
            float title_w = estimate_text_w_px(title);
            draw_text(win_w * 0.5f - title_w * 0.5f, start_y, title, 1.0f, 1.0f, 1.0f, 1.0f);
            
            start_y += 50.0f;
            
            // Botoes do menu de pausa
            struct PauseButton { std::string text; int id; };
            PauseButton buttons[] = {
                {"Continuar", 0},
                {"Salvar Jogo", 1},
                {"Carregar Jogo", 2},
                {"Configuracoes", 3},
                {"Novo Jogo", 4}
            };
            
            g_pause_selection = -1;  // Reset selection
            
            for (int i = 0; i < 5; ++i) {
                float by = start_y + i * (btn_h + btn_gap);
                bool hovered = mouse_in_rect(center_x, by, btn_w, btn_h);
                if (hovered) g_pause_selection = buttons[i].id;
                draw_mc_button(center_x, by, btn_w, btn_h, buttons[i].text, hovered);
            }
            
            // Separador
            start_y += 5 * (btn_h + btn_gap) + 15.0f;
            render_quad(center_x, start_y, btn_w, 2.0f, 0.5f, 0.5f, 0.55f, 0.6f);
            
            // Controles (texto menor)
            start_y += 15.0f;
            draw_text(center_x, start_y, "CONTROLES:", 0.75f, 0.80f, 0.90f, 0.90f);
            start_y += 22.0f;
            draw_text(center_x, start_y, "WASD - Mover", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Espaco - Pular  |  Shift - Correr", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Botao Direito - Rotacionar Camera", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Scroll - Zoom  |  1-9 - Selecionar Item", 0.65f, 0.65f, 0.70f, 0.85f);
            start_y += 18.0f;
            draw_text(center_x, start_y, "Botao Esquerdo - Minerar/Construir", 0.65f, 0.65f, 0.70f, 0.85f);
            
        } else if (g_state == GameState::Menu) {
            // === MENU PRINCIPAL ESTILO MINECRAFT ===
            float btn_w = 320.0f;
            float btn_h = 45.0f;
            float btn_gap = 10.0f;
            
            // Logo/Titulo grande
            std::string title = "TERRAFORMER";
            float title_w = estimate_text_w_px(title);
            // Sombra do titulo
            draw_text(win_w * 0.5f - title_w * 0.5f + 3.0f, win_h * 0.18f + 3.0f, title, 0.15f, 0.15f, 0.15f, 0.9f);
            // Titulo principal
            draw_text(win_w * 0.5f - title_w * 0.5f, win_h * 0.18f, title, 0.95f, 0.85f, 0.25f, 1.0f);
            
            // Subtitulo
            std::string subtitle = "Colonize. Construa. Terraforma.";
            float sub_w = estimate_text_w_px(subtitle);
            draw_text(win_w * 0.5f - sub_w * 0.5f, win_h * 0.18f + 35.0f, subtitle, 0.70f, 0.75f, 0.80f, 0.90f);
            
            float start_y = win_h * 0.38f;
            float center_x = win_w * 0.5f - btn_w * 0.5f;
            
            // Botoes do menu principal
            struct MenuButton { std::string text; int id; };
            MenuButton buttons[] = {
                {"Novo Jogo", 0},
                {"Carregar Jogo", 1},
                {"Sair", 2}
            };
            
            g_menu_selection = -1;  // Reset selection
            
            for (int i = 0; i < 3; ++i) {
                float by = start_y + i * (btn_h + btn_gap);
                bool hovered = mouse_in_rect(center_x, by, btn_w, btn_h);
                if (hovered) g_menu_selection = buttons[i].id;
                draw_mc_button(center_x, by, btn_w, btn_h, buttons[i].text, hovered);
            }
            
            // Versao no canto
            draw_text(10.0f, win_h - 20.0f, "TerraFormer v1.0", 0.5f, 0.5f, 0.55f, 0.7f);
        }
    }
    
    // Death screen
    if (g_state == GameState::Dead) {
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.15f, 0.0f, 0.0f, 0.75f);
        std::string title = "VOCE MORREU";
        draw_text(win_w * 0.5f - estimate_text_w_px(title) * 0.5f, win_h * 0.35f, title, 0.95f, 0.25f, 0.25f, 0.98f);
        draw_text(win_w * 0.5f - estimate_text_w_px(g_toast) * 0.5f, win_h * 0.35f + 40.0f, g_toast, 0.90f, 0.90f, 0.90f, 0.95f);
        draw_text(win_w * 0.5f - 100.0f, win_h * 0.35f + 90.0f, "Enter - Novo Jogo", 0.90f, 0.90f, 0.90f, 0.95f);
        draw_text(win_w * 0.5f - 100.0f, win_h * 0.35f + 115.0f, "Esc - Menu Principal", 0.90f, 0.90f, 0.90f, 0.95f);
    }
    
    // Settings menu
    if (g_state == GameState::Settings) {
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.85f);
        
        float menu_w = 480.0f;
        float menu_h = 520.0f;  // Aumentado para opcoes de iluminacao
        float menu_x = win_w * 0.5f - menu_w * 0.5f;
        float menu_y = win_h * 0.5f - menu_h * 0.5f;
        
        // Background panel
        render_quad(menu_x, menu_y, menu_w, menu_h, 0.08f, 0.10f, 0.14f, 0.98f);
        render_quad(menu_x, menu_y, menu_w, 4.0f, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        render_quad(menu_x, menu_y + menu_h - 4.0f, menu_w, 4.0f, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        render_quad(menu_x, menu_y, 4.0f, menu_h, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        render_quad(menu_x + menu_w - 4.0f, menu_y, 4.0f, menu_h, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        
        std::string title = "CONFIGURACOES";
        draw_text(win_w * 0.5f - estimate_text_w_px(title) * 0.5f, menu_y + 25.0f, title, 0.95f, 0.95f, 0.95f, 1.0f);
        
        float row_y = menu_y + 70.0f;
        float row_h = 40.0f;
        float label_x = menu_x + 30.0f;
        float value_x = menu_x + 280.0f;
        
        // Opcao: Sensibilidade da Camera
        bool sel0 = (g_settings_selection == 0);
        if (sel0) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Sensibilidade Camera", sel0 ? 1.0f : 0.8f, sel0 ? 1.0f : 0.8f, sel0 ? 1.0f : 0.8f, 1.0f);
        char sens_buf[32];
        snprintf(sens_buf, sizeof(sens_buf), "< %.2f >", g_settings.camera_sensitivity);
        draw_text(value_x, row_y + 5.0f, sens_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Inverter Y
        bool sel1 = (g_settings_selection == 1);
        if (sel1) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Inverter Eixo Y", sel1 ? 1.0f : 0.8f, sel1 ? 1.0f : 0.8f, sel1 ? 1.0f : 0.8f, 1.0f);
        const char* invert_str = g_settings.invert_y ? "Sim" : "Nao";
        draw_text(value_x, row_y + 5.0f, invert_str, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Brilho
        bool sel2 = (g_settings_selection == 2);
        if (sel2) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Brilho", sel2 ? 1.0f : 0.8f, sel2 ? 1.0f : 0.8f, sel2 ? 1.0f : 0.8f, 1.0f);
        char bright_buf[32];
        snprintf(bright_buf, sizeof(bright_buf), "< %.0f%% >", g_settings.brightness * 100.0f);
        draw_text(value_x, row_y + 5.0f, bright_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Escala UI
        bool sel3 = (g_settings_selection == 3);
        if (sel3) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Escala UI", sel3 ? 1.0f : 0.8f, sel3 ? 1.0f : 0.8f, sel3 ? 1.0f : 0.8f, 1.0f);
        char scale_buf[32];
        snprintf(scale_buf, sizeof(scale_buf), "< %.0f%% >", g_settings.ui_scale * 100.0f);
        draw_text(value_x, row_y + 5.0f, scale_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // === OPCOES DE ILUMINACAO (RTX FAKE) ===
        draw_text(label_x, row_y + 5.0f, "--- Iluminacao RTX ---", 0.9f, 0.75f, 0.3f, 0.9f);
        row_y += row_h * 0.7f;
        
        // Opcao: Iluminacao Ativada
        bool sel4 = (g_settings_selection == 4);
        if (sel4) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Iluminacao 2D", sel4 ? 1.0f : 0.8f, sel4 ? 1.0f : 0.8f, sel4 ? 1.0f : 0.8f, 1.0f);
        const char* light_str = g_lighting.enabled ? "Ativada" : "Desativada";
        draw_text(value_x, row_y + 5.0f, light_str, g_lighting.enabled ? 0.3f : 0.8f, g_lighting.enabled ? 0.9f : 0.4f, 0.3f, 1.0f);
        row_y += row_h;
        
        // Opcao: Sombras
        bool sel5 = (g_settings_selection == 5);
        if (sel5) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Sombras 2D", sel5 ? 1.0f : 0.8f, sel5 ? 1.0f : 0.8f, sel5 ? 1.0f : 0.8f, 1.0f);
        const char* shadow_str = g_lighting.shadows_enabled ? "Ativadas" : "Desativadas";
        draw_text(value_x, row_y + 5.0f, shadow_str, g_lighting.shadows_enabled ? 0.3f : 0.8f, g_lighting.shadows_enabled ? 0.9f : 0.4f, 0.3f, 1.0f);
        row_y += row_h;
        
        // Opcao: Bloom
        bool sel6 = (g_settings_selection == 6);
        if (sel6) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Bloom/Glow", sel6 ? 1.0f : 0.8f, sel6 ? 1.0f : 0.8f, sel6 ? 1.0f : 0.8f, 1.0f);
        char bloom_buf[32];
        snprintf(bloom_buf, sizeof(bloom_buf), "< %.0f%% >", g_lighting.bloom_intensity * 100.0f);
        draw_text(value_x, row_y + 5.0f, bloom_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Vinheta
        bool sel7 = (g_settings_selection == 7);
        if (sel7) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Vinheta", sel7 ? 1.0f : 0.8f, sel7 ? 1.0f : 0.8f, sel7 ? 1.0f : 0.8f, 1.0f);
        char vignette_buf[32];
        snprintf(vignette_buf, sizeof(vignette_buf), "< %.0f%% >", g_lighting.vignette_intensity * 100.0f);
        draw_text(value_x, row_y + 5.0f, vignette_buf, kColorPanelBorder[0], kColorPanelBorder[1], kColorPanelBorder[2], 1.0f);
        row_y += row_h;
        
        // Opcao: Voltar
        bool sel8 = (g_settings_selection == 8);
        if (sel8) render_quad(menu_x + 10.0f, row_y - 5.0f, menu_w - 20.0f, row_h, 0.25f, 0.45f, 0.70f, 0.5f);
        draw_text(label_x, row_y + 5.0f, "Voltar", sel8 ? 1.0f : 0.8f, sel8 ? 1.0f : 0.8f, sel8 ? 1.0f : 0.8f, 1.0f);
        
        // Instrucoes
        draw_text(menu_x + 30.0f, menu_y + menu_h - 40.0f, "W/S: Navegar | A/D: Ajustar | Esc/Enter: Voltar | F3: Debug Lightmap", 0.6f, 0.65f, 0.70f, 0.9f);
    }

    if (g_victory) {
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.18f);
        std::string t2 = "Terraformacao Completa!";
        draw_text(win_w * 0.5f - estimate_text_w_px(t2) * 0.5f, win_h * 0.20f, t2, 0.85f, 0.95f, 0.85f, 0.98f);
    }
    
    // ============= BUILD MENU =============
    if (g_show_build_menu && g_state == GameState::Playing) {
        float menu_w = 850.0f;
        float menu_h = 650.0f;
        float menu_x = win_w * 0.5f - menu_w * 0.5f;
        float menu_y = win_h * 0.5f - menu_h * 0.5f;
        
        // Background
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.70f);
        render_quad(menu_x, menu_y, menu_w, menu_h, 0.05f, 0.07f, 0.10f, 0.98f);
        
        // Border
        render_quad(menu_x, menu_y, menu_w, 3.0f, 0.30f, 0.55f, 0.85f, 1.0f);
        render_quad(menu_x, menu_y + menu_h - 3.0f, menu_w, 3.0f, 0.30f, 0.55f, 0.85f, 1.0f);
        render_quad(menu_x, menu_y, 3.0f, menu_h, 0.30f, 0.55f, 0.85f, 1.0f);
        render_quad(menu_x + menu_w - 3.0f, menu_y, 3.0f, menu_h, 0.30f, 0.55f, 0.85f, 1.0f);
        
        // Title
        std::string title = "MENU DE CONSTRUCAO";
        draw_text(menu_x + menu_w * 0.5f - estimate_text_w_px(title) * 0.5f, menu_y + 25.0f, title, 0.95f, 0.95f, 0.95f, 1.0f);
        draw_text(menu_x + menu_w * 0.5f - 150.0f, menu_y + 45.0f, "Tab/B: Fechar  |  W/S: Selecionar  |  Enter: Construir", 0.55f, 0.60f, 0.70f, 0.85f);
        
        // Module types available
        Block module_types[] = {
            Block::SolarPanel,
            Block::EnergyGenerator,
            Block::OxygenGenerator,
            Block::WaterExtractor,
            Block::Greenhouse,
            Block::Workshop,
            Block::CO2Factory,
            Block::Habitat,
            Block::TerraformerBeacon,
        };
        const int module_count = 9;
        
        // Clamp selection
        if (g_build_menu_selection < 0) g_build_menu_selection = 0;
        if (g_build_menu_selection >= module_count) g_build_menu_selection = module_count - 1;
        
        float list_x = menu_x + 15.0f;
        float list_y = menu_y + 65.0f;
        float row_h = 58.0f;
        float list_w = menu_w - 250.0f;
        
        for (int i = 0; i < module_count; ++i) {
            Block mtype = module_types[i];
            ModuleStats stats = get_module_stats(mtype);
            CraftCost cost = get_module_cost(mtype);
            bool affordable = can_afford(cost);
            bool selected = (i == g_build_menu_selection);
            
            // Check if under construction
            bool building = false;
            float build_progress = 0.0f;
            for (const auto& job : g_construction_queue) {
                if (job.active && job.module_type == mtype) {
                    building = true;
                    build_progress = 1.0f - (job.time_remaining / job.total_time);
                    break;
                }
            }
            
            // Count how many of this module we have
            int count = 0;
            for (const auto& mod : g_modules) {
                if (mod.type == mtype) count++;
            }
            
            // Determine status
            const char* status_str;
            float stat_r, stat_g, stat_b;
            if (building) {
                status_str = "CONSTRUINDO";
                stat_r = 0.95f; stat_g = 0.75f; stat_b = 0.20f;
            } else if (affordable) {
                status_str = "DISPONIVEL";
                stat_r = 0.30f; stat_g = 0.90f; stat_b = 0.40f;
            } else {
                status_str = "BLOQUEADO";
                stat_r = 0.80f; stat_g = 0.40f; stat_b = 0.35f;
            }
            
            // Row background
            float bg_alpha = selected ? 0.40f : 0.15f;
            float bg_r = selected ? 0.12f : 0.08f;
            float bg_g = selected ? 0.22f : 0.10f;
            float bg_b = selected ? 0.38f : 0.15f;
            render_quad(list_x, list_y, list_w, row_h - 3.0f, bg_r, bg_g, bg_b, bg_alpha);
            
            // Selection indicator
            if (selected) {
                render_quad(list_x, list_y, 4.0f, row_h - 3.0f, 0.35f, 0.75f, 0.95f, 1.0f);
            }
            
            // Build progress bar if building
            if (building) {
                render_quad(list_x + 4.0f, list_y + row_h - 8.0f, (list_w - 8.0f) * build_progress, 4.0f, 0.30f, 0.80f, 0.50f, 0.90f);
            }
            
            // Module name and count
            float name_r = affordable ? 0.95f : 0.60f;
            float name_g = affordable ? 0.95f : 0.60f;
            float name_b = affordable ? 0.95f : 0.65f;
            std::string name_str = std::string(stats.name);
            if (count > 0) name_str += " [" + std::to_string(count) + " ativo]";
            draw_text(list_x + 12.0f, list_y + 16.0f, name_str, name_r, name_g, name_b, 1.0f);
            
            // Description
            draw_text(list_x + 12.0f, list_y + 32.0f, stats.description, 0.55f, 0.60f, 0.70f, 0.80f);
            
            // Production/Consumption info
            std::string prod_str;
            if (stats.energy_production > 0.0f) prod_str += "+" + std::to_string((int)stats.energy_production) + " Energia/min ";
            if (stats.oxygen_production > 0.0f) prod_str += "+" + std::to_string((int)(stats.oxygen_production*10)/10.0f).substr(0,3) + " O2/min ";
            if (stats.water_production > 0.0f) prod_str += "+" + std::to_string((int)(stats.water_production*10)/10.0f).substr(0,3) + " Agua/min ";
            if (stats.food_production > 0.0f) prod_str += "+" + std::to_string((int)(stats.food_production*10)/10.0f).substr(0,3) + " Comida/min ";
            if (stats.integrity_bonus > 0.0f) prod_str += "+" + std::to_string((int)stats.integrity_bonus) + " Reparo/min ";
            if (prod_str.empty()) prod_str = "Terraformacao";
            
            std::string cons_str;
            if (stats.energy_consumption > 0.0f) cons_str = "-" + std::to_string((int)(stats.energy_consumption*10)/10.0f).substr(0,3) + " Energia/min";
            
            draw_text(list_x + 220.0f, list_y + 16.0f, prod_str, 0.35f, 0.80f, 0.45f, 0.85f);
            if (!cons_str.empty()) {
                draw_text(list_x + 220.0f, list_y + 32.0f, cons_str, 0.85f, 0.55f, 0.35f, 0.80f);
            }
            
            // Status
            draw_text(list_x + list_w - 95.0f, list_y + 16.0f, status_str, stat_r, stat_g, stat_b, 0.95f);
            
            // Cost
            std::string cost_str = module_cost_string(cost);
            float cost_r = affordable ? 0.50f : 0.75f;
            float cost_g = affordable ? 0.80f : 0.50f;
            float cost_b = affordable ? 0.55f : 0.45f;
            draw_text(list_x + 12.0f, list_y + 46.0f, cost_str, cost_r, cost_g, cost_b, 0.75f);
            
            // Construction time
            std::string time_str = "Tempo: " + std::to_string((int)stats.construction_time) + "s";
            draw_text(list_x + list_w - 95.0f, list_y + 32.0f, time_str, 0.60f, 0.65f, 0.70f, 0.75f);
            
            list_y += row_h;
        }
        
        // === RIGHT SIDE: BASE STATUS ===
        float status_x = menu_x + menu_w - 225.0f;
        float status_y = menu_y + 65.0f;
        
        render_quad(status_x - 5.0f, status_y - 5.0f, 220.0f, 250.0f, 0.08f, 0.10f, 0.14f, 0.90f);
        draw_text(status_x + 55.0f, status_y + 12.0f, "STATUS DA BASE", 0.85f, 0.90f, 0.95f, 0.95f);
        status_y += 30.0f;
        
        // Base resources with detailed bars
        auto draw_status_bar = [&](const char* label, float value, float max_val, float r, float g, float b) {
            float pct = std::clamp(value / max_val, 0.0f, 1.0f);
            render_quad(status_x, status_y, 200.0f, 18.0f, 0.12f, 0.12f, 0.18f, 0.85f);
            render_quad(status_x + 1.0f, status_y + 1.0f, 198.0f * pct, 16.0f, r, g, b, 0.90f);
            std::string txt = std::string(label) + ": " + std::to_string((int)value) + "/" + std::to_string((int)max_val);
            draw_text(status_x + 5.0f, status_y + 13.0f, txt, 0.95f, 0.95f, 0.95f, 0.98f);
            status_y += 24.0f;
        };
        
        draw_status_bar("Energia", g_base_energy, kBaseEnergyMax, 0.95f, 0.80f, 0.20f);
        draw_status_bar("Agua", g_base_water, kBaseWaterMax, 0.25f, 0.60f, 0.95f);
        draw_status_bar("Oxigenio", g_base_oxygen, kBaseOxygenMax, 0.25f, 0.90f, 0.50f);
        draw_status_bar("Comida", g_base_food, kBaseFoodMax, 0.85f, 0.60f, 0.25f);
        
        // Integrity bar
        float int_r = g_base_integrity > 50.0f ? 0.30f : (g_base_integrity > 25.0f ? 0.90f : 0.95f);
        float int_g = g_base_integrity > 50.0f ? 0.85f : (g_base_integrity > 25.0f ? 0.70f : 0.30f);
        float int_b = g_base_integrity > 50.0f ? 0.40f : 0.20f;
        draw_status_bar("Integridade", g_base_integrity, kBaseIntegrityMax, int_r, int_g, int_b);
        
        // Consumption info
        status_y += 10.0f;
        draw_text(status_x, status_y, "CONSUMO CONSTANTE:", 0.70f, 0.75f, 0.85f, 0.80f);
        status_y += 18.0f;
        draw_text(status_x, status_y, "-1 O2/min  -2 Energia/min  -1 Agua/min", 0.85f, 0.55f, 0.45f, 0.75f);
        
        // === BOTTOM: INVENTORY ===
        float bottom_y = menu_y + menu_h - 90.0f;
        render_quad(menu_x + 10.0f, bottom_y, menu_w - 20.0f, 80.0f, 0.08f, 0.10f, 0.14f, 0.90f);
        draw_text(menu_x + 20.0f, bottom_y + 15.0f, "SEU INVENTARIO:", 0.80f, 0.85f, 0.95f, 0.92f);
        
        std::string res_line1 = 
            "Pedra: " + std::to_string(g_inventory[(int)Block::Stone]) + 
            "   Ferro: " + std::to_string(g_inventory[(int)Block::Iron]) + 
            "   Cobre: " + std::to_string(g_inventory[(int)Block::Copper]) +
            "   Gelo: " + std::to_string(g_inventory[(int)Block::Ice]);
        std::string res_line2 = 
            "Carvao: " + std::to_string(g_inventory[(int)Block::Coal]) + 
            "   Cristal: " + std::to_string(g_inventory[(int)Block::Crystal]) +
            "   Metal: " + std::to_string(g_inventory[(int)Block::Metal]) +
            "   Organico: " + std::to_string(g_inventory[(int)Block::Organic]) +
            "   Comp: " + std::to_string(g_inventory[(int)Block::Components]);
        draw_text(menu_x + 20.0f, bottom_y + 38.0f, res_line1, 0.90f, 0.92f, 0.95f, 0.95f);
        draw_text(menu_x + 20.0f, bottom_y + 58.0f, res_line2, 0.90f, 0.92f, 0.95f, 0.95f);
    }
    
    // ============= ALERTS DISPLAY =============
    if (!g_alerts.empty() && g_state == GameState::Playing && !g_show_build_menu) {
        float alert_y = 150.0f;
        for (const auto& alert : g_alerts) {
            float alpha = std::min(1.0f, alert.time_remaining);
            float alert_w = estimate_text_w_px(alert.message) + 30.0f;
            float alert_x = win_w - alert_w - 20.0f;
            
            render_quad(alert_x, alert_y, alert_w, 28.0f, alert.r * 0.3f, alert.g * 0.3f, alert.b * 0.3f, 0.85f * alpha);
            render_quad(alert_x, alert_y, 4.0f, 28.0f, alert.r, alert.g, alert.b, alpha);
            draw_text(alert_x + 15.0f, alert_y + 19.0f, alert.message, alert.r, alert.g, alert.b, alpha);
            
            alert_y += 35.0f;
        }
    }

    // === MAPA GRANDE (sobreposicao) ===
    if (g_minimap.world_map_open) {
        render_world_map(win_w, win_h);
    }

    // Resetar clique do mouse no final do frame
    g_mouse_left_clicked = false;

    SwapBuffers(hdc);
}

// ============= Input State =============
static bool key_down(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static bool key_pressed(int vk, bool& prev) {
    bool cur = key_down(vk);
    bool pressed = cur && !prev;
    prev = cur;
    return pressed;
}

// ============= Update =============
static void update_game(float dt, HWND hwnd) {
    if (!g_world) return;

    // Toast timer
    if (g_toast_time > 0.0f) g_toast_time -= dt;
    
    // ============= ATUALIZAR FEEDBACK VISUAL =============
    if (g_screen_flash_red > 0.0f) g_screen_flash_red -= dt * 2.5f;
    if (g_screen_flash_green > 0.0f) g_screen_flash_green -= dt * 2.5f;
    if (g_unlock_popup_timer > 0.0f) g_unlock_popup_timer -= dt;
    if (g_hotbar_bounce > 0.0f) g_hotbar_bounce -= dt * 4.0f;
    
    // Atualizar popups de coleta
    for (auto& popup : g_collect_popups) {
        popup.life -= dt;
        popup.y -= dt * 30.0f;  // Flutua para cima
    }
    g_collect_popups.erase(
        std::remove_if(g_collect_popups.begin(), g_collect_popups.end(),
            [](const CollectPopup& p) { return p.life <= 0.0f; }),
        g_collect_popups.end());
    
    // Atualizar onboarding
    update_onboarding(dt);
    
    // Atualizar fog of war do minimapa
    update_fog_of_war();

    // Stats timer (periodically recompute terraform score)
    g_stats_timer += dt;
    if (g_stats_timer >= 2.0f || g_surface_dirty) {
        g_stats_timer = 0.0f;
        g_surface_dirty = false;
        recompute_terraform_score(*g_world);
    }

    // Hotkey states
    bool esc_pressed = key_pressed(VK_ESCAPE, g_prev_esc);
    bool enter_pressed = key_pressed(VK_RETURN, g_prev_enter);
    bool f5_pressed = key_pressed(VK_F5, g_prev_f5);
    bool f9_pressed = key_pressed(VK_F9, g_prev_f9);
    bool l_pressed = key_pressed('L', g_prev_l);
    bool q_pressed = key_pressed('Q', g_prev_q);
    bool f3_pressed = key_pressed(VK_F3, g_prev_f3);
    bool f6_pressed = key_pressed(VK_F6, g_prev_f6);
    bool f7_pressed = key_pressed(VK_F7, g_prev_f7);
    bool h_pressed = key_pressed('H', g_prev_h);
    bool tab_pressed = key_pressed(VK_TAB, g_prev_tab);
    bool b_pressed = key_pressed('B', g_prev_b);
    bool m_pressed = key_pressed('M', g_prev_m);
    bool r_pressed = key_pressed('R', g_prev_r);
    bool c_key_pressed = key_pressed('C', g_prev_c);
    
    // === MAPA GRANDE (tecla M) ===
    if (m_pressed && g_state == GameState::Playing) {
        g_minimap.world_map_open = !g_minimap.world_map_open;
        if (g_minimap.world_map_open) {
            // Centralizar no jogador ao abrir
            g_minimap.world_pan_x = g_player.pos.x;
            g_minimap.world_pan_y = g_player.pos.y;
            g_minimap.world_zoom = 1.0f;
        }
    }
    
    // Controles do mapa grande
    if (g_minimap.world_map_open && g_state == GameState::Playing) {
        // ESC fecha o mapa
        if (esc_pressed) {
            g_minimap.world_map_open = false;
            g_prev_esc = true;  // Consumir o ESC para nao pausar
        }
        
        // WASD para mover o mapa
        float pan_speed = g_map_cfg.world_map_pan_speed * dt / g_minimap.world_zoom;
        if (key_down('W') || key_down(VK_UP)) g_minimap.world_pan_y -= pan_speed;
        if (key_down('S') || key_down(VK_DOWN)) g_minimap.world_pan_y += pan_speed;
        if (key_down('A') || key_down(VK_LEFT)) g_minimap.world_pan_x -= pan_speed;
        if (key_down('D') || key_down(VK_RIGHT)) g_minimap.world_pan_x += pan_speed;
        
        // Limitar pan aos limites do mundo
        g_minimap.world_pan_x = std::clamp(g_minimap.world_pan_x, 0.0f, (float)g_world->w);
        g_minimap.world_pan_y = std::clamp(g_minimap.world_pan_y, 0.0f, (float)g_world->h);
        
        // Clique para adicionar waypoint
        if (g_mouse_left_clicked) {
            // Converter posicao do mouse para coordenadas do mundo
            RECT rc;
            GetClientRect(hwnd, &rc);
            int win_w = rc.right - rc.left;
            int win_h = rc.bottom - rc.top;
            
            float map_margin = 50.0f;
            float map_w = (float)win_w - map_margin * 2.0f;
            float map_h = (float)win_h - map_margin * 2.0f - 50.0f;
            float map_x = map_margin;
            float map_y = map_margin;
            
            // Verificar se clicou dentro do mapa
            if (g_mouse_x >= map_x && g_mouse_x <= map_x + map_w &&
                g_mouse_y >= map_y && g_mouse_y <= map_y + map_h) {
                
                float zoom = g_minimap.world_zoom;
                float tiles_visible_x = (float)g_world->w / zoom;
                float tiles_visible_y = (float)g_world->h / zoom;
                
                float map_aspect = map_w / map_h;
                float world_aspect = tiles_visible_x / tiles_visible_y;
                if (map_aspect > world_aspect) {
                    tiles_visible_x = tiles_visible_y * map_aspect;
                } else {
                    tiles_visible_y = tiles_visible_x / map_aspect;
                }
                
                float start_world_x = g_minimap.world_pan_x - tiles_visible_x * 0.5f;
                float start_world_y = g_minimap.world_pan_y - tiles_visible_y * 0.5f;
                
                float px_per_tile_x = map_w / tiles_visible_x;
                float px_per_tile_y = map_h / tiles_visible_y;
                
                int world_x = (int)(start_world_x + (g_mouse_x - map_x) / px_per_tile_x);
                int world_y = (int)(start_world_y + (g_mouse_y - map_y) / px_per_tile_y);
                
                if (g_world->in_bounds(world_x, world_y)) {
                    add_waypoint(world_x, world_y);
                }
            }
            g_mouse_left_clicked = false;
        }
        
        // R para remover waypoint mais proximo do centro da visao
        if (r_pressed) {
            remove_nearest_waypoint((int)g_minimap.world_pan_x, (int)g_minimap.world_pan_y);
        }
        
        // C para limpar todos os waypoints
        if (c_key_pressed) {
            clear_all_waypoints();
        }
        
        // Nao processar movimento do jogador enquanto mapa esta aberto
        return;
    }

    // F3 alterna entre modos de debug: normal -> lightmap -> lights -> off
    if (f3_pressed) {
        if (!g_debug && !g_debug_lightmap && !g_debug_lights) {
            g_debug = true;  // Primeiro: debug basico
        } else if (g_debug && !g_debug_lightmap) {
            g_debug = false;
            g_debug_lightmap = true;  // Segundo: lightmap
        } else if (g_debug_lightmap && !g_debug_lights) {
            g_debug_lightmap = false;
            g_debug_lights = true;  // Terceiro: luzes
        } else {
            g_debug = false;
            g_debug_lightmap = false;
            g_debug_lights = false;  // Desliga tudo
        }
    }

    // State machine
    if (g_state == GameState::Menu) {
        // Clique do mouse nos botoes do menu principal
        if (g_mouse_left_clicked && g_menu_selection >= 0) {
            g_mouse_left_clicked = false;
            switch (g_menu_selection) {
                case 0:  // Novo Jogo
                    delete g_world;
                    g_world = new World(WORLD_WIDTH, WORLD_HEIGHT, (unsigned)GetTickCount());
                    spawn_player_new_game(*g_world);
                    g_cam_pos = g_player.pos;
                    g_day_time = kDayLength * 0.25f;
                    g_modules.clear();
                    g_particles.clear();
                    g_shooting_stars.clear();
                    g_construction_queue.clear();
                    g_alerts.clear();
                    g_build_slots.clear();
                    g_collect_popups.clear();
                    g_drops.clear();
                    g_onboarding = OnboardingState();
                    g_state = GameState::Playing;
                    show_tip("WASD para mover, Espaco para pular, Botao direito para girar camera", g_onboarding.shown_first_move);
                    return;
                case 1:  // Carregar Jogo
                    if (load_game(kSavePath)) {
                        set_toast("Jogo carregado!");
                        g_state = GameState::Playing;
                    } else {
                        set_toast("Nenhum save encontrado.");
                    }
                    return;
                case 2:  // Sair
                    g_quit = true;
                    return;
            }
        }
        
        if (esc_pressed) {
            g_quit = true;
            return;
        }
        if (enter_pressed) {
            delete g_world;
            g_world = new World(WORLD_WIDTH, WORLD_HEIGHT, (unsigned)GetTickCount());
            spawn_player_new_game(*g_world);  // This sets O2, water, etc. to 100%
            g_cam_pos = g_player.pos;
            g_day_time = kDayLength * 0.25f;  // Start at morning
            g_modules.clear();
            g_particles.clear();
            g_shooting_stars.clear();
            g_construction_queue.clear();
            g_alerts.clear();
            g_build_slots.clear();
            g_collect_popups.clear();
            g_drops.clear();
            
            // Reset onboarding para novo jogo
            g_onboarding = OnboardingState();
            
            g_state = GameState::Playing;
            
            // Dica inicial de onboarding
            show_tip("WASD para mover, Espaco para pular, Botao direito para girar camera", g_onboarding.shown_first_move);
            return;
        }
        if (l_pressed || f9_pressed) {
            if (load_game(kSavePath)) {
                set_toast("Jogo carregado!");
                g_state = GameState::Playing;
            } else {
                set_toast("Nenhum save encontrado.");
            }
            return;
        }
        return;
    }

    if (g_state == GameState::Paused) {
        // Clique do mouse nos botoes do menu de pausa
        if (g_mouse_left_clicked && g_pause_selection >= 0) {
            g_mouse_left_clicked = false;
            switch (g_pause_selection) {
                case 0:  // Continuar
                    g_state = GameState::Playing;
                    return;
                case 1:  // Salvar Jogo
                    if (save_game(kSavePath)) set_toast("Jogo salvo!");
                    else set_toast("Falha ao salvar!");
                    return;
                case 2:  // Carregar Jogo
                    if (load_game(kSavePath)) {
                        set_toast("Jogo carregado!");
                        g_state = GameState::Playing;
                    } else {
                        set_toast("Falha ao carregar!");
                    }
                    return;
                case 3:  // Configuracoes
                    g_state = GameState::Settings;
                    g_settings_selection = 0;
                    return;
                case 4:  // Novo Jogo
                    g_state = GameState::Menu;
                    return;
            }
        }
        
        if (esc_pressed) {
            g_state = GameState::Playing;
            return;
        }
        if (q_pressed) {
            g_state = GameState::Menu;
            return;
        }
        // Tecla 'O' abre configuracoes
        if (GetAsyncKeyState('O') & 0x8000) {
            static bool o_was_pressed = false;
            if (!o_was_pressed) {
                g_state = GameState::Settings;
                g_settings_selection = 0;
                o_was_pressed = true;
            }
        } else {
            static bool o_was_pressed = false;
            o_was_pressed = false;
        }
        if (f5_pressed) {
            if (save_game(kSavePath)) set_toast("Jogo salvo!");
            else set_toast("Falha ao salvar!");
            return;
        }
        if (f9_pressed) {
            if (load_game(kSavePath)) {
                set_toast("Jogo carregado!");
                g_state = GameState::Playing;
            } else {
                set_toast("Falha ao carregar!");
            }
            return;
        }
        return;
    }
    
    // Menu de configuracoes
    if (g_state == GameState::Settings) {
        static bool key_w_held = false;
        static bool key_s_held = false;
        static bool key_a_held = false;
        static bool key_d_held = false;
        
        bool w_now = (GetAsyncKeyState('W') & 0x8000) != 0;
        bool s_now = (GetAsyncKeyState('S') & 0x8000) != 0;
        bool a_now = (GetAsyncKeyState('A') & 0x8000) != 0;
        bool d_now = (GetAsyncKeyState('D') & 0x8000) != 0;
        
        // Navegar para cima
        if (w_now && !key_w_held) {
            g_settings_selection = (g_settings_selection - 1 + 9) % 9;
        }
        key_w_held = w_now;
        
        // Navegar para baixo
        if (s_now && !key_s_held) {
            g_settings_selection = (g_settings_selection + 1) % 9;
        }
        key_s_held = s_now;
        
        // F3 para debug lightmap
        static bool f3_held = false;
        bool f3_now = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
        if (f3_now && !f3_held) {
            g_debug_lightmap = !g_debug_lightmap;
        }
        f3_held = f3_now;
        
        // Ajustar valores
        float delta = 0.0f;
        if (a_now && !key_a_held) delta = -1.0f;
        if (d_now && !key_d_held) delta = 1.0f;
        key_a_held = a_now;
        key_d_held = d_now;
        
        if (delta != 0.0f) {
            switch (g_settings_selection) {
                case 0: // Sensibilidade
                    g_settings.camera_sensitivity = std::clamp(g_settings.camera_sensitivity + delta * 0.02f, 0.05f, 0.5f);
                    g_camera.sensitivity = g_settings.camera_sensitivity;
                    break;
                case 1: // Inverter Y
                    g_settings.invert_y = !g_settings.invert_y;
                    break;
                case 2: // Brilho
                    g_settings.brightness = std::clamp(g_settings.brightness + delta * 0.1f, 0.5f, 1.5f);
                    break;
                case 3: // Escala UI
                    g_settings.ui_scale = std::clamp(g_settings.ui_scale + delta * 0.1f, 0.75f, 1.5f);
                    break;
                case 4: // Iluminacao 2D
                    g_lighting.enabled = !g_lighting.enabled;
                    break;
                case 5: // Sombras
                    g_lighting.shadows_enabled = !g_lighting.shadows_enabled;
                    break;
                case 6: // Bloom
                    g_lighting.bloom_intensity = std::clamp(g_lighting.bloom_intensity + delta * 0.1f, 0.0f, 1.0f);
                    g_lighting.bloom_enabled = (g_lighting.bloom_intensity > 0.0f);
                    break;
                case 7: // Vinheta
                    g_lighting.vignette_intensity = std::clamp(g_lighting.vignette_intensity + delta * 0.1f, 0.0f, 0.6f);
                    break;
                case 8: // Voltar
                    break;
            }
        }
        
        // ESC ou Enter no "Voltar" fecha o menu
        if (esc_pressed || (enter_pressed && g_settings_selection == 8)) {
            g_state = GameState::Paused;
            return;
        }
        return;
    }
    
    if (g_state == GameState::Dead) {
        // Death screen - wait for Enter to start new game
        if (enter_pressed) {
            delete g_world;
            g_world = new World(WORLD_WIDTH, WORLD_HEIGHT, (unsigned)GetTickCount());
            spawn_player_new_game(*g_world);
            g_cam_pos = g_player.pos;
            g_day_time = kDayLength * 0.25f;
            g_modules.clear();
            g_particles.clear();
            g_drops.clear();
            g_construction_queue.clear();
            g_alerts.clear();
            g_build_slots.clear();
            g_state = GameState::Playing;
            set_toast("Novo jogo!");
            return;
        }
        if (esc_pressed) {
            g_state = GameState::Menu;
            return;
        }
        return;
    }

    // Playing state
    
    // ESC fecha menu de construcao ou pausa o jogo
    if (esc_pressed) {
        if (g_show_build_menu) {
            g_show_build_menu = false;  // ESC fecha menu de construcao
            return;
        }
        g_state = GameState::Paused;
        return;
    }
    
    // Toggle build menu with Tab or B
    if (tab_pressed || b_pressed) {
        g_show_build_menu = !g_show_build_menu;
        if (g_show_build_menu) {
            g_build_menu_selection = 0;
            // Onboarding: dica ao abrir menu de construcao pela primeira vez
            if (!g_onboarding.shown_first_build_menu) {
                show_tip("W/S para navegar, Enter para construir, ESC para fechar", g_onboarding.shown_first_build_menu);
            }
        }
        return;
    }
    
    // Build menu navigation and actions
    if (g_show_build_menu) {
        static bool prev_w = false, prev_s = false, prev_enter = false;
        bool w_now = key_down('W') || key_down(VK_UP);
        bool s_now = key_down('S') || key_down(VK_DOWN);
        bool enter_now = key_down(VK_RETURN);
        
        // Module types list (matches render order)
        const Block module_types[] = {
            Block::SolarPanel, Block::EnergyGenerator, Block::OxygenGenerator,
            Block::WaterExtractor, Block::Greenhouse, Block::Workshop,
            Block::CO2Factory, Block::Habitat, Block::TerraformerBeacon
        };
        const int module_count = 9;
        
        // Navigate up (W ou seta para cima)
        if (w_now && !prev_w) {
            g_build_menu_selection--;
            if (g_build_menu_selection < 0) 
                g_build_menu_selection = module_count - 1;
            bounce_hotbar_slot(g_build_menu_selection);  // Feedback visual
        }
        // Navigate down (S ou seta para baixo)
        if (s_now && !prev_s) {
            g_build_menu_selection++;
            if (g_build_menu_selection >= module_count) 
                g_build_menu_selection = 0;
            bounce_hotbar_slot(g_build_menu_selection);  // Feedback visual
        }
        // Build action with construction time
        if (enter_now && !prev_enter && g_build_menu_selection >= 0 && 
            g_build_menu_selection < module_count) {
            
            Block module_type = module_types[g_build_menu_selection];
            CraftCost cost = get_module_cost(module_type);
            
            // Check if already under construction
            bool already_building = false;
            for (const auto& job : g_construction_queue) {
                if (job.active && job.module_type == module_type) {
                    already_building = true;
                    break;
                }
            }
            
            if (already_building) {
                show_error("Ja em construcao!");
            } else if (can_afford(cost)) {
                // Find a free slot for this module (or create at base)
                int slot_index = -1;
                
                // Try to find an empty slot
                for (int si = 0; si < (int)g_build_slots.size(); ++si) {
                    if (g_build_slots[si].assigned_module == Block::Air) {
                        slot_index = si;
                        break;
                    }
                }
                
                // If no slot, create a new one near base
                if (slot_index < 0) {
                    // Find empty spot near base
                    for (int dx = -30; dx <= 30; ++dx) {
                        int tx = g_base_x + dx;
                        if (tx < 0 || tx >= g_world->w) continue;
                        int ty = g_base_y - 1;
                        Block current = g_world->get(tx, ty);
                        if (current == Block::Air || current == Block::BuildSlot) {
                            BuildSlotInfo new_slot;
                            new_slot.x = tx;
                            new_slot.y = ty;
                            new_slot.assigned_module = Block::Air;
                            new_slot.label = "Auto";
                            g_build_slots.push_back(new_slot);
                            slot_index = (int)g_build_slots.size() - 1;
                            break;
                        }
                    }
                }
                
                if (slot_index >= 0) {
                    // Start construction (will take time!)
                    start_construction(module_type, slot_index);
                    g_build_slots[slot_index].assigned_module = module_type;
                } else {
                    show_error("Sem espaco para construir!");
                }
            } else {
                show_error("Recursos insuficientes!");
            }
        }
        
        prev_w = w_now;
        prev_s = s_now;
        prev_enter = enter_now;
        return;  // Don't process other inputs while in menu
    }
    
    // Return to base with H
    if (h_pressed) {
        spawn_player_at_base();
        set_toast("Retornou a base!");
        return;
    }

    if (f7_pressed) {
        reload_physics_config(true);
        reload_terrain_config(true);
        reload_sky_config(true);
        reload_camera_config(true);
        reload_mining_config(true);
        reload_player_visual_config(true);
        reset_player_physics_runtime(false);
        set_toast(std::string("Configs recarregadas: ") + g_physics_config_path + " | " + g_terrain_config_path + " | " + g_sky_config_path + " | " + g_camera_config_path + " | " + g_mining_config_path + " | " + g_player_visual_config_path, 3.5f);
    }

    if (f6_pressed) {
        build_physics_test_map(*g_world);
        return;
    }

    // Update modules (energy/water/oxygen production, terraforming)
    update_modules(*g_world, dt);

    // Hotbar selection
    // Resources: 1-6
    const Block resource_slots[] = {Block::Dirt, Block::Stone, Block::Iron, Block::Copper, Block::Coal, Block::Wood};
    for (int i = 0; i < 6; ++i) {
        if (key_down('1' + i)) g_selected = resource_slots[i];
    }
    
    // Modules: 7-0 (dynamically based on unlocks)
    std::vector<Block> module_slots;
    if (g_unlocks.solar_unlocked) module_slots.push_back(Block::SolarPanel);
    if (g_unlocks.water_extractor_unlocked) module_slots.push_back(Block::WaterExtractor);
    if (g_unlocks.o2_generator_unlocked) module_slots.push_back(Block::OxygenGenerator);
    if (g_unlocks.greenhouse_unlocked) module_slots.push_back(Block::Greenhouse);
    if (g_unlocks.co2_factory_unlocked) module_slots.push_back(Block::CO2Factory);
    if (g_unlocks.habitat_unlocked) module_slots.push_back(Block::Habitat);
    if (g_unlocks.terraformer_unlocked) module_slots.push_back(Block::TerraformerBeacon);
    
    for (int i = 0; i < (int)module_slots.size() && i < 4; ++i) {
        int key = (i < 3) ? ('7' + i) : '0';
        if (key_down(key)) g_selected = module_slots[i];
    }

    // ============= MOVIMENTO 3D (TIMESTEP FIXO) =============
    float cam_yaw_rad = g_camera.yaw * (kPi / 180.0f);
    float cam_forward_x = -std::sin(cam_yaw_rad);
    float cam_forward_z = -std::cos(cam_yaw_rad);
    float cam_right_x = std::cos(cam_yaw_rad);
    float cam_right_z = -std::sin(cam_yaw_rad);

    float input_forward = 0.0f;
    float input_right = 0.0f;
    if (key_down('W') || key_down(VK_UP)) input_forward += 1.0f;
    if (key_down('S') || key_down(VK_DOWN)) input_forward -= 1.0f;
    if (key_down('A') || key_down(VK_LEFT)) input_right -= 1.0f;
    if (key_down('D') || key_down(VK_RIGHT)) input_right += 1.0f;

    Vec2 move_world = {
        input_forward * cam_forward_x + input_right * cam_right_x,
        input_forward * cam_forward_z + input_right * cam_right_z
    };
    bool has_input = (move_world.x != 0.0f || move_world.y != 0.0f);
    if (has_input) move_world = vec2_normalize(move_world);

    bool run_key = key_down(VK_SHIFT);
    bool jump_held = key_down(VK_SPACE);
    bool jump_pressed = jump_held && !g_physics.jump_was_held;
    bool jump_released = !jump_held && g_physics.jump_was_held;
    g_physics.jump_was_held = jump_held;

    PlayerPhysicsInput physics_input{};
    physics_input.move = move_world;
    physics_input.has_move = has_input;
    physics_input.run = run_key;
    physics_input.jump_pressed = jump_pressed;
    physics_input.jump_held = jump_held;
    physics_input.jump_released = jump_released;
    step_player_physics(physics_input, dt);

    if (key_down(VK_ADD) || key_down(VK_OEM_PLUS)) {
        g_camera.distance = std::max(g_camera.min_distance, g_camera.distance - 10.0f * dt);
    }
    if (key_down(VK_SUBTRACT) || key_down(VK_OEM_MINUS)) {
        g_camera.distance = std::min(g_camera.max_distance, g_camera.distance + 10.0f * dt);
    }

    g_player.anim_frame += dt;
    g_player.is_moving = vec2_length(g_player.vel) > 0.15f;
    if (g_player.is_moving) g_player.walk_timer += dt * (run_key ? 1.5f : 1.0f);
    else g_player.walk_timer *= 0.9f;
    
    // === SURVIVAL MECHANICS ===
    // Astronaut dies from: no oxygen OR no water (after 30 seconds without)
    static float dehydration_timer = 0.0f;  // Time without water
    static float suffocation_timer = 0.0f;  // Time without oxygen
    static float damage_tick = 0.0f;
    
    const float kDamageDelay = 15.0f;  // 15 seconds before damage starts (was 30s)
    
    // Track time without resources
    if (g_water_res <= 0.0f) {
        dehydration_timer += dt;
    } else {
        dehydration_timer = 0.0f; // Reset when water is available
    }
    
    if (g_oxygen <= 0.0f) {
        suffocation_timer += dt;
    } else {
        suffocation_timer = 0.0f; // Reset when oxygen is available
    }
    
    // Damage tick (every second)
    damage_tick += dt;
    if (damage_tick >= 1.0f) {
        damage_tick = 0.0f;
        
        // Suffocation damage - only after 30 seconds without oxygen
        if (suffocation_timer > kDamageDelay) {
            g_player.hp = std::max(0, g_player.hp - 10);
            if (g_player.hp <= 0) {
                respawn_player_at_base("Sufocamento");
                return;
            }
        }
        
        // Dehydration damage - only after 30 seconds without water
        if (dehydration_timer > kDamageDelay) {
            g_player.hp = std::max(0, g_player.hp - 8);
            if (g_player.hp <= 0) {
                respawn_player_at_base("Desidratacao");
                return;
            }
        }
    }
    
    // Warnings when resources are empty (before damage starts)
    // Increased interval from 3s to 5s to reduce spam
    static float warn_timer = 0.0f;
    warn_timer += dt;
    if (warn_timer >= 5.0f) {
        warn_timer = 0.0f;
        
        if (g_oxygen <= 0.0f && suffocation_timer < kDamageDelay) {
            int seconds_left = (int)(kDamageDelay - suffocation_timer);
            set_toast("SEM OXIGENIO! Dano em " + std::to_string(seconds_left) + "s!", 2.5f);
        } else if (g_water_res <= 0.0f && dehydration_timer < kDamageDelay) {
            int seconds_left = (int)(kDamageDelay - dehydration_timer);
            set_toast("SEM AGUA! Dano em " + std::to_string(seconds_left) + "s!", 2.5f);
        } else if (g_oxygen < 15.0f && g_oxygen > 0.0f) {
            set_toast("Aviso: Oxigenio baixo! Construa Gerador de O2.");
            // Onboarding: dica para voltar a base
            if (!g_onboarding.shown_return_to_base) {
                show_tip("H para voltar a base e recarregar oxigenio", g_onboarding.shown_return_to_base);
            }
        } else if (g_water_res < 15.0f && g_water_res > 0.0f) {
            set_toast("Aviso: Agua baixa! Construa Extrator de Agua.");
            // Onboarding: dica para agua baixa
            if (!g_onboarding.shown_low_water) {
                show_tip("Quebre blocos de gelo para obter agua", g_onboarding.shown_low_water);
            }
        }
    }

    // Camera follow sincronizado com interpolacao da fisica
    float cam_speed = 6.0f;
    Vec2 render_pos = get_player_render_pos();
    g_cam_pos.x = approach(g_cam_pos.x, render_pos.x, cam_speed * dt * std::fabs(render_pos.x - g_cam_pos.x) + 0.5f * dt);
    g_cam_pos.y = approach(g_cam_pos.y, render_pos.y, cam_speed * dt * std::fabs(render_pos.y - g_cam_pos.y) + 0.5f * dt);

    // Mouse targeting
    POINT cursor;
    GetCursorPos(&cursor);
    ScreenToClient(hwnd, &cursor);

    RECT rc;
    GetClientRect(hwnd, &rc);
    int win_w = rc.right - rc.left;
    int win_h = rc.bottom - rc.top;

    // Atualizar camera antes do targeting, para a mira (+) do centro bater com o raycast.
    update_camera_for_frame();

    // ============= TARGETING 3D (Estilo Minicraft) =============
    // A mira (+) segue o mouse; fazemos raycast a partir da camera na direcao do mouse.
    const float kReach = 4.2f; // alcance de interacao (minerar/colocar)

    g_has_target = false;
    g_target_in_range = false;
    g_has_place_target = false;
    g_place_in_range = false;
    g_target_drop = -1;

    auto placeable_tile = [&](Block b) -> bool {
        if (is_base_structure(b)) return false;
        if (is_module(b)) return false;
        if (b == Block::Air || b == Block::Water) return true;
        return !is_solid(b); // walkable pode ser substituido
    };

    auto blocks_raycast = [&](Block b) -> bool {
        if (b == Block::Air) return false;
        if (is_ground_like(b)) return true; // permite selecionar/minerar o chao corretamente
        if (b == Block::Water) return true;
        if (b == Block::Leaves) return true;
        if (is_base_structure(b)) return true;
        if (is_module(b)) return true;
        return is_solid(b);
    };

    // Ray da camera (mira) - agora baseado na posicao do mouse
    Vec3 ray_o = g_camera.position;
    Vec3 ray_d = get_mouse_ray_direction(cursor.x, cursor.y, win_w, win_h);
    float ray_max = std::clamp(g_camera.effective_distance + kReach + 3.0f, 8.0f, 55.0f);

    // Primeiro: tentar mirar um drop (para facilitar coleta visual)
    {
        float best_t = std::numeric_limits<float>::infinity();
        float best_perp2 = 0.0f;
        for (int i = 0; i < (int)g_drops.size(); ++i) {
            const ItemDrop& d = g_drops[(size_t)i];
            Vec3 c = {d.x, d.y, d.z};
            Vec3 rel = vec3_sub(c, ray_o);
            float t = vec3_dot(rel, ray_d);
            if (t < 0.2f || t > ray_max) continue;
            Vec3 closest = vec3_add(ray_o, vec3_scale(ray_d, t));
            Vec3 diff = vec3_sub(c, closest);
            float perp2 = vec3_dot(diff, diff);

            // "hitbox" da mira para o drop (um pouco generoso)
            if (perp2 <= 0.26f * 0.26f) {
                // E so considera se o drop nao estiver muito longe do player (alcance real)
                float dx = d.x - g_player.pos.x;
                float dz = d.z - g_player.pos.y;
                float d2 = dx * dx + dz * dz;
                if (d2 <= (kReach + 1.5f) * (kReach + 1.5f)) {
                    if (t < best_t || (std::fabs(t - best_t) < 0.15f && perp2 < best_perp2)) {
                        best_t = t;
                        best_perp2 = perp2;
                        g_target_drop = i;
                    }
                }
            }
        }
    }

    int last_place_x = -1;
    int last_place_y = -1;

    auto ray_aabb_hit = [&](const Vec3& bmin, const Vec3& bmax, float& out_t) -> bool {
        float tmin = 0.0f;
        float tmax = ray_max;
        const float eps = 1e-6f;
        auto axis_test = [&](float ro, float rd, float mn, float mx) -> bool {
            if (std::fabs(rd) < eps) {
                return ro >= mn && ro <= mx;
            }
            float inv = 1.0f / rd;
            float t1 = (mn - ro) * inv;
            float t2 = (mx - ro) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            return tmin <= tmax;
        };

        if (!axis_test(ray_o.x, ray_d.x, bmin.x, bmax.x)) return false;
        if (!axis_test(ray_o.y, ray_d.y, bmin.y, bmax.y)) return false;
        if (!axis_test(ray_o.z, ray_d.z, bmin.z, bmax.z)) return false;

        out_t = (tmin >= 0.0f) ? tmin : tmax;
        return out_t >= 0.0f && out_t <= ray_max;
    };

    auto ray_hits_tile = [&](int tx, int tz, Block b, float& out_t) -> bool {
        float base_y = (float)g_world->height_at(tx, tz) * kHeightScale;
        Vec3 bmin = {tile_min(tx), base_y - 0.05f, tile_min(tz)};
        Vec3 bmax = {tile_max(tx), base_y + 1.05f, tile_max(tz)};

        if (b == Block::Water) {
            bmin.y = base_y - 0.30f;
            bmax.y = base_y + 0.08f;
        } else if (b == Block::Leaves) {
            bmin.y = base_y + 0.45f;
            bmax.y = base_y + 0.82f;
        } else if (is_ground_like(b)) {
            // Coluna baixa para permitir selecionar topo e paredes de buracos.
            bmin.y = base_y - 1.05f;
            bmax.y = base_y + 0.10f;
        }
        return ray_aabb_hit(bmin, bmax, out_t);
    };

    // Raymarch por tiles + teste preciso de intersecao com AABB do alvo.
    int prev_tx = std::numeric_limits<int>::min();
    int prev_tz = std::numeric_limits<int>::min();
    for (float t = 0.20f; t <= ray_max; t += 0.05f) {
        Vec3 p = vec3_add(ray_o, vec3_scale(ray_d, t));
        int tx = world_to_tile(p.x);
        int tz = world_to_tile(p.z);
        if (tx == prev_tx && tz == prev_tz) continue;
        prev_tx = tx;
        prev_tz = tz;

        if (!g_world->in_bounds(tx, tz)) break;

        Block top_b = g_world->get(tx, tz);
        Block b = (top_b == Block::Air) ? surface_block_at(*g_world, tx, tz) : top_b;

        if (placeable_tile(top_b)) {
            last_place_x = tx;
            last_place_y = tz;
        }

        float hit_t = 0.0f;
        if (!blocks_raycast(b) || !ray_hits_tile(tx, tz, b, hit_t)) continue;

        g_target_x = tx;
        g_target_y = tz;
        g_has_target = true;

        float dx = tile_center(g_target_x) - g_player.pos.x;
        float dz = tile_center(g_target_y) - g_player.pos.y;
        float dist = std::sqrt(dx * dx + dz * dz);
        g_target_in_range = (dist <= kReach);

        // Alvo de colocacao: tile atual se substituivel, ou o ultimo substituivel antes do hit.
        if (placeable_tile(top_b)) {
            g_place_x = tx;
            g_place_y = tz;
            g_has_place_target = true;
            g_place_in_range = g_target_in_range;
        } else if (last_place_x != -1) {
            g_place_x = last_place_x;
            g_place_y = last_place_y;
            g_has_place_target = true;
            float pdx = tile_center(g_place_x) - g_player.pos.x;
            float pdz = tile_center(g_place_y) - g_player.pos.y;
            g_place_in_range = (std::sqrt(pdx * pdx + pdz * pdz) <= kReach);
        }

        if (!g_onboarding.shown_first_mine && is_mineable(b)) {
            show_tip("Segure clique esquerdo (ou E) para minerar blocos", g_onboarding.shown_first_mine);
        }
        break;
    }

    // Sem hit: mantem selecao apenas no ultimo tile substituivel realmente visto.
    if (!g_has_target && last_place_x != -1) {
        g_target_x = last_place_x;
        g_target_y = last_place_y;
        g_has_target = true;
        float dx = tile_center(g_target_x) - g_player.pos.x;
        float dz = tile_center(g_target_y) - g_player.pos.y;
        g_target_in_range = (std::sqrt(dx * dx + dz * dz) <= kReach);

        g_place_x = g_target_x;
        g_place_y = g_target_y;
        g_has_place_target = true;
        g_place_in_range = g_target_in_range;
    }

    // Fallback: se nao houver tile de colocacao direto, tenta um adjacente do alvo atual.
    if (!g_has_place_target && g_has_target) {
        float best_d2 = std::numeric_limits<float>::infinity();
        int best_x = -1;
        int best_y = -1;
        for (int oz = -1; oz <= 1; ++oz) {
            for (int ox = -1; ox <= 1; ++ox) {
                if (ox == 0 && oz == 0) continue;
                int tx = g_target_x + ox;
                int tz = g_target_y + oz;
                if (!g_world->in_bounds(tx, tz)) continue;
                Block nb = g_world->get(tx, tz);
                if (!placeable_tile(nb)) continue;
                float dx = tile_center(tx) - g_player.pos.x;
                float dz = tile_center(tz) - g_player.pos.y;
                float d2 = dx * dx + dz * dz;
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best_x = tx;
                    best_y = tz;
                }
            }
        }
        if (best_x != -1) {
            g_place_x = best_x;
            g_place_y = best_y;
            g_has_place_target = true;
            g_place_in_range = (best_d2 <= kReach * kReach);
        }
    }

    // Cooldowns (apenas colocacao)
    if (g_place_cd > 0.0f) g_place_cd -= dt;

    bool lmb = key_down(VK_LBUTTON);
    bool rmb = key_down(VK_RBUTTON);

    bool e_key = key_down('E');
    g_prev_e = e_key;

    // Mining com progresso (segurar LMB ou E)
    bool mine_input = (lmb || e_key);
    bool has_mine_target = g_has_target && g_target_in_range && g_world->in_bounds(g_target_x, g_target_y);
    Block mine_block = has_mine_target ? g_world->get(g_target_x, g_target_y) : Block::Air;
    if (has_mine_target && mine_block == Block::Air) {
        mine_block = surface_block_at(*g_world, g_target_x, g_target_y);
    }

    // Feedback ao tentar minerar estrutura da base
    if (mine_input && has_mine_target && is_base_structure(mine_block)) {
        static float base_warn_cd = 0.0f;
        base_warn_cd -= dt;
        if (base_warn_cd <= 0.0f) {
            show_error("Nao pode destruir estruturas da base!");
            base_warn_cd = 1.0f;
        }
    }

    bool mine_ok = mine_input && has_mine_target && is_mineable(mine_block);
    if (mine_ok && is_ground_like(mine_block)) {
        int16_t h = g_world->height_at(g_target_x, g_target_y);
        if (h <= 0) mine_ok = false; // bedrock local: evita mineracao infinita
    }
    static float mining_particle_timer = 0.0f;
    if (mine_ok) {
        g_player.is_mining = true;
        g_player.mine_anim = std::max(g_player.mine_anim - dt * 7.5f, 0.0f);

        // Virar na direcao do alvo
        float dx = tile_center(g_target_x) - g_player.pos.x;
        float dz = tile_center(g_target_y) - g_player.pos.y;
        g_player.target_rotation = std::atan2(-dx, -dz) * (180.0f / kPi);
        if (g_player.target_rotation < 0.0f) g_player.target_rotation += 360.0f;

        // Se mudou de bloco alvo, resetar progresso/hits.
        if (g_target_x != g_mine_block_x || g_target_y != g_mine_block_y) {
            g_mine_block_x = g_target_x;
            g_mine_block_y = g_target_y;
            g_mine_progress = 0.0f;
            g_mine_hits = 0;
            g_mine_hit_timer = 0.0f;
        }

        int req_hits = std::max(1, block_hits_required(mine_block));
        float stage = clamp01(g_terraform / 100.0f);
        float speed_mult = lerp(g_mining_cfg.early_game_speed_mult, g_mining_cfg.late_game_speed_mult, stage);
        speed_mult = std::max(0.25f, speed_mult);
        float hit_interval = std::max(g_mining_cfg.hit_interval_min, g_mining_cfg.hit_interval / speed_mult);

        g_mine_hit_timer -= dt;
        if (g_mine_hit_timer <= 0.0f) {
            g_mine_hit_timer = hit_interval;
            g_mine_hits = std::min(req_hits, g_mine_hits + 1);
            g_mine_progress = (float)g_mine_hits / (float)req_hits;

            // Impacto visivel por hit.
            g_player.mine_anim = std::max(g_player.mine_anim, g_player_visual_cfg.mine_impact_amp);
            g_screen_flash_green = std::max(g_screen_flash_green, 0.07f);
            Beep(880, 1);

            // Particulas por golpe.
            for (int i = 0; i < 4; ++i) {
                Particle p;
                p.pos.x = tile_center(g_target_x) + (rand() % 100 - 50) / 100.0f * 0.42f;
                p.pos.y = tile_center(g_target_y) + (rand() % 100 - 50) / 100.0f * 0.42f;
                p.vel.x = (rand() % 100 - 50) / 45.0f;
                p.vel.y = (rand() % 100 - 50) / 45.0f - 1.2f;
                p.life = 0.22f + (rand() % 16) / 100.0f;
                float br, bg, bb, ba;
                block_color(mine_block, g_target_y, g_world->h, br, bg, bb, ba);
                p.r = br * 0.85f + 0.15f;
                p.g = bg * 0.85f + 0.15f;
                p.b = bb * 0.85f + 0.15f;
                p.a = 0.95f;
                g_particles.push_back(p);
            }
        }

        // Particulas de mineracao (feedback visual constante)
        mining_particle_timer += dt;
        if (mining_particle_timer >= 0.08f) {
            mining_particle_timer = 0.0f;
            if (mine_block != Block::Air) {
                for (int i = 0; i < 2; ++i) {
                    Particle p;
                    p.pos.x = tile_center(g_target_x) + (rand() % 100 - 50) / 100.0f * 0.4f;
                    p.pos.y = tile_center(g_target_y) + (rand() % 100 - 50) / 100.0f * 0.4f;
                    p.vel.x = (rand() % 100 - 50) / 50.0f;
                    p.vel.y = (rand() % 100 - 50) / 50.0f - 1.0f;
                    p.life = 0.3f + (rand() % 20) / 100.0f;
                    float br, bg, bb, ba;
                    block_color(mine_block, g_target_y, g_world->h, br, bg, bb, ba);
                    p.r = br * 0.9f + 0.1f;
                    p.g = bg * 0.9f + 0.1f;
                    p.b = bb * 0.9f + 0.1f;
                    p.a = 0.9f;
                    g_particles.push_back(p);
                }
            }
        }

        // Quebrar bloco ao completar progresso
        if (g_mine_hits >= req_hits || g_mine_progress >= 0.999f) {
            Block b = mine_block;
            spawn_block_particles(b, tile_center(g_target_x), tile_center(g_target_y), g_world->h);

            // Para blocos de terreno, remover 1 bloco "inteiro" em altura de mundo.
            // Como o heightmap usa kHeightScale, convertemos 1.0 mundo -> unidades do heightmap.
            if (is_ground_like(b)) {
                constexpr float kWorldBlockHeight = 1.0f;
                int dig_units = std::max(1, (int)std::lround(kWorldBlockHeight / std::max(0.01f, kHeightScale)));
                int16_t h = g_world->height_at(g_target_x, g_target_y);
                int nh = std::max(0, (int)h - dig_units);
                g_world->set_height(g_target_x, g_target_y, (int16_t)nh);

                // Mantem material de solo (ground-like) para permitir mineracao sequencial.
                Block prev_ground = g_world->get_ground(g_target_x, g_target_y);
                Block next_ground = Block::Dirt;
                if (prev_ground == Block::Sand) next_ground = Block::Sand;
                else if (prev_ground == Block::Snow || prev_ground == Block::Ice) next_ground = Block::Ice;
                g_world->set_ground(g_target_x, g_target_y, next_ground);
                g_world->set(g_target_x, g_target_y, next_ground);
            } else {
                g_world->set(g_target_x, g_target_y, Block::Air);
            }

            g_surface_dirty = true;

            if (is_module(b)) {
                refund_cost(module_cost(b));
                g_modules.erase(std::remove_if(g_modules.begin(), g_modules.end(),
                    [](const Module& m) { return m.x == g_target_x && m.y == g_target_y; }), g_modules.end());
            } else {
                Block drop = drop_item_for_block(b);
                float sy = (float)g_world->height_at(g_target_x, g_target_y) * kHeightScale + drop_spawn_y_for_block(b);
                spawn_item_drop(drop, tile_center(g_target_x), tile_center(g_target_y), sy);
            }

            // Reset apos quebrar
            g_mine_progress = 0.0f;
            g_mine_block_x = -1;
            g_mine_block_y = -1;
            g_mine_hits = 0;
            g_mine_hit_timer = 0.0f;
            mining_particle_timer = 0.0f;
        }
    } else {
        g_player.is_mining = false;
        g_player.mine_anim = std::max(g_player.mine_anim - dt * 8.0f, 0.0f);
        mining_particle_timer = 0.0f;
        g_mine_progress = 0.0f;
        g_mine_block_x = -1;
        g_mine_block_y = -1;
        g_mine_hits = 0;
        g_mine_hit_timer = 0.0f;
    }

    g_prev_lmb = lmb;

    // Placing (RMB)
    auto placeable_tile_for_place = [&](Block b) -> bool {
        if (is_base_structure(b)) return false;
        if (is_module(b)) return false;
        if (b == Block::Air || b == Block::Water) return true;
        return !is_solid(b); // chao/walkable pode ser substituido
    };

    if (rmb && !g_prev_rmb && g_has_place_target && g_place_in_range && g_place_cd <= 0.0f) {
        Block cur = g_world->get(g_place_x, g_place_y);
        if (placeable_tile_for_place(cur)) {
            // Check player collision
            float pl = g_player.pos.x - g_player.w * 0.5f;
            float pr = g_player.pos.x + g_player.w * 0.5f;
            float pt = g_player.pos.y - g_player.h * 0.5f;
            float pb = g_player.pos.y + g_player.h * 0.5f;
            float tl = tile_min(g_place_x);
            float tr = tile_max(g_place_x);
            float tf = tile_min(g_place_y);
            float tb = tile_max(g_place_y);
            bool overlaps_player = !(tr <= pl || tl >= pr || tb <= pt || tf >= pb);

            if (!overlaps_player) {
                if (is_module(g_selected)) {
                    // Check if module is unlocked first
                    if (!is_unlocked(g_selected)) {
                        set_toast("Modulo nao desbloqueado! Colete mais recursos.");
                    } else {
                        CraftCost cost = module_cost(g_selected);
                        if (can_afford(cost)) {
                            spend_cost(cost);
                            g_world->set(g_place_x, g_place_y, g_selected);
                            g_modules.push_back(Module{g_place_x, g_place_y, g_selected, 0.0f});
                            g_surface_dirty = true;
                            g_place_cd = 0.25f;
                            
                            // Special messages for certain modules
                            if (g_selected == Block::CO2Factory) {
                                set_toast("Fabrica de CO2 colocada! Aquecendo o planeta...", 3.0f);
                            } else if (g_selected == Block::TerraformerBeacon) {
                                show_success("Terraformador ativo! (Requer fase de Degelo)");
                            }
                        } else {
                            show_error("Recursos insuficientes!");
                        }
                    }
                } else if (g_inventory[(int)g_selected] > 0) {
                    g_inventory[(int)g_selected]--;
                    g_world->set(g_place_x, g_place_y, g_selected);
                    g_surface_dirty = true;
                    g_place_cd = 0.12f;
                }
            }
        }
    }
    g_prev_rmb = rmb;

    // Atualizar drops e coletar por proximidade
    update_item_drops(dt);

    // Update particles
    for (auto& p : g_particles) {
        p.vel.y += 15.0f * dt;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.life -= dt;
    }
    g_particles.erase(std::remove_if(g_particles.begin(), g_particles.end(),
        [](const Particle& p) { return p.life <= 0.0f; }), g_particles.end());
}

// ============= Window Procedure =============
// Variaveis para controle de camera com mouse
static int g_last_mouse_x = 0;
static int g_last_mouse_y = 0;
static bool g_mouse_captured = false;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            g_quit = true;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && g_state == GameState::Menu) {
                g_quit = true;
            }
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            
            // Se mapa grande esta aberto, controlar zoom do mapa
            if (g_minimap.world_map_open) {
                float zoom_delta = (float)delta * 0.001f;
                g_minimap.world_zoom += zoom_delta;
                g_minimap.world_zoom = std::clamp(g_minimap.world_zoom, 
                    g_map_cfg.world_map_zoom_min, g_map_cfg.world_map_zoom_max);
            } else {
                // Zoom da camera com scroll do mouse
                g_camera.distance -= (float)delta * 0.005f;
                g_camera.distance = std::clamp(g_camera.distance, g_camera.min_distance, g_camera.max_distance);
            }
            return 0;
        }
        case WM_MBUTTONDOWN:
            // Capturar mouse ao clicar com botao do meio para rotacionar camera
            g_mouse_captured = true;
            SetCapture(hwnd);
            ShowCursor(FALSE);
            return 0;
        case WM_MBUTTONUP:
            // Liberar mouse
            g_mouse_captured = false;
            ReleaseCapture();
            ShowCursor(TRUE);
            return 0;
        case WM_RBUTTONDOWN:
            // Clique direito do mouse - usado para construir (processado no update)
            g_mouse_x = LOWORD(lParam);
            g_mouse_y = HIWORD(lParam);
            return 0;
        case WM_LBUTTONDOWN:
            // Clique esquerdo do mouse - usado para selecionar/minerar
            g_mouse_left_clicked = true;
            g_mouse_x = LOWORD(lParam);
            g_mouse_y = HIWORD(lParam);
            return 0;
        case WM_MOUSEMOVE:
            // Sempre atualiza posicao do mouse
            g_mouse_x = LOWORD(lParam);
            g_mouse_y = HIWORD(lParam);
            
            if (g_mouse_captured && g_state == GameState::Playing) {
                int mx = LOWORD(lParam);
                int my = HIWORD(lParam);
                
                int delta_x = mx - g_last_mouse_x;
                int delta_y = my - g_last_mouse_y;
                
                // Rotacionar camera (mouse direita = camera gira direita)
                g_camera.yaw += delta_x * g_camera.sensitivity;
                g_camera.pitch -= delta_y * g_camera.sensitivity * 0.5f;
                
                // Clamp pitch
                g_camera.pitch = std::clamp(g_camera.pitch, g_camera.min_pitch, g_camera.max_pitch);
                
                // Normalizar yaw
                while (g_camera.yaw >= 360.0f) g_camera.yaw -= 360.0f;
                while (g_camera.yaw < 0.0f) g_camera.yaw += 360.0f;
                
                // Recentrar o mouse
                RECT rc;
                GetClientRect(hwnd, &rc);
                POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
                ClientToScreen(hwnd, &center);
                SetCursorPos(center.x, center.y);
                
                ScreenToClient(hwnd, &center);
                g_last_mouse_x = center.x;
                g_last_mouse_y = center.y;
            } else {
                g_last_mouse_x = LOWORD(lParam);
                g_last_mouse_y = HIWORD(lParam);
            }
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ============= WinMain =============
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Register window class
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "TerraFormer2DClass";

    if (!RegisterClassA(&wc)) {
        MessageBoxA(nullptr, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create window
    int win_w = 1280;
    int win_h = 720;
    RECT wr = {0, 0, win_w, win_h};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowA(
        "TerraFormer2DClass",
        "TerraFormer 2D",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBoxA(nullptr, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    HDC hdc = GetDC(hwnd);
    HGLRC hrc = setup_opengl(hdc);
    init_font(hdc);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    reload_physics_config(true);
    reload_terrain_config(true);
    reload_sky_config(true);
    reload_camera_config(true);
    reload_mining_config(true);
    reload_player_visual_config(true);

    // Initialize world for menu background
    g_world = new World(WORLD_WIDTH, WORLD_HEIGHT, 1337);
    spawn_player_new_game(*g_world);
    g_cam_pos = g_player.pos;
    g_state = GameState::Menu;

    // Timing
    LARGE_INTEGER freq, last_time, cur_time;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last_time);

    // Main loop
    MSG msg;
    while (!g_quit) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (g_quit) break;

        // Calculate delta time
        QueryPerformanceCounter(&cur_time);
        float dt = (float)(cur_time.QuadPart - last_time.QuadPart) / (float)freq.QuadPart;
        last_time = cur_time;
        dt = std::clamp(dt, 0.0001f, 0.1f); // Clamp to avoid huge jumps

        // Update
        update_game(dt, hwnd);

        // Render
        RECT rc;
        GetClientRect(hwnd, &rc);
        render_world(hdc, rc.right - rc.left, rc.bottom - rc.top);

        // Small sleep to avoid 100% CPU
        Sleep(1);
    }

    // Cleanup
    delete g_world;
    g_world = nullptr;

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(hrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);

    return 0;
}
