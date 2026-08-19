#include "raylib_platform.h"
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
#include "ui_hud.h"              // render_hud (ui_hud extraction stage)
#include "ui_menu.h"             // render_menus/update_menu_input (ui_menu extraction stage)
#include "building_interaction.h" // render_build_menu/update_build_menu_input/update_mining_and_placement (building_interaction extraction stage)
#include "input.h"                // key_down/key_pressed (input extraction stage)
#include "win32_platform.h"       // WindowProc/WinMain (win32_platform extraction stage - see there for why it declares nothing)
#include "objectives.h"           // objectives_victory_celebration_remaining (player objectives feature)

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
// owned here (not moved with the feedback subsystem into game_state.cpp), but it lost
// "static": ui_menu.cpp's render_menus()/update_menu_input() (the ui_menu extraction
// stage - Settings screen render + A/D value adjustment) now read/write it from another
// translation unit - same pattern as g_terrain_cfg/g_base_cfg etc. above.
GameSettings g_settings;

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
// g_settings_selection/g_pause_selection/g_menu_selection lost "static" here: ui_menu.cpp's
// render_menus() (button-hover highlighting) and update_menu_input() (click/keyboard
// handling, the ui_menu extraction stage) read/write them from another translation unit now.
int g_settings_selection = 0;  // 0=sensibilidade, 1=inverter Y, 2=brilho, 3=escala UI, 4=iluminacao, 5=sombras, 6=bloom, 7=vinheta, 8=voltar
int g_pause_selection = -1;     // -1=nenhum, 0=continuar, 1=salvar, 2=carregar, 3=config, 4=novo jogo
int g_menu_selection = -1;      // -1=nenhum, 0=novo jogo, 1=carregar, 2=sair

// Posicao do mouse na tela. g_mouse_x/g_mouse_y/g_mouse_left_clicked lost "static" here:
// ui_hud.cpp's render_hud() (extracted this stage - hotbar slot hit-testing, crosshair)
// needs external linkage to read them - same pattern as g_oxygen/g_water_res/etc. in
// textures.cpp.
int g_mouse_x = 0;
int g_mouse_y = 0;
bool g_mouse_left_clicked = false;  // Flag para clique esquerdo (single frame)

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
// g_quit lost "static" here: ui_menu.cpp's update_menu_input() (Sair/ESC-from-main-menu
// handling, the ui_menu extraction stage) sets it from another translation unit now - the
// WinMain message loop and WindowProc below (still in this file) keep reading/writing it too.
bool g_quit = false;
static const int WORLD_WIDTH = 768;
static const int WORLD_HEIGHT = 384;
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

// g_prev_lmb/g_prev_rmb/g_prev_e lost "static" here: building_interaction.cpp's
// update_mining_and_placement() (the building_interaction extraction stage) needs external
// linkage to read/write them from another translation unit - same pattern as
// g_oxygen/g_water_res/etc. in textures.cpp.
bool g_prev_lmb = false;
bool g_prev_rmb = false;
static bool g_prev_esc = false;
static bool g_prev_enter = false;
bool g_prev_e = false;  // Tecla de interacao (top-down)
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

// g_place_cd lost "static" here: building_interaction.cpp's update_mining_and_placement()
// needs external linkage to read/write it from another translation unit - same pattern as
// g_prev_lmb above.
float g_place_cd = 0.0f;
static float g_drown_accum = 0.0f;

// Mining progress (estilo Minicraft/Minecraft: segurar para quebrar). All five lost
// "static" here: building_interaction.cpp's update_mining_and_placement() needs external
// linkage to read/write them from another translation unit - same pattern as g_prev_lmb
// above.
int g_mine_block_x = -1;
int g_mine_block_y = -1;
float g_mine_progress = 0.0f; // 0..1
int g_mine_hits = 0;
float g_mine_hit_timer = 0.0f;

// g_has_target/g_target_x/g_target_y/g_target_in_range lost "static" here: ui_hud.cpp's
// render_hud() (extracted this stage - target-info HUD line) needs external linkage to
// read them - same pattern as g_oxygen/g_water_res/etc. in textures.cpp.
bool g_has_target = false;
int g_target_x = 0;
int g_target_y = 0;
bool g_target_in_range = false;

// Target de colocacao (tile onde o RMB vai tentar colocar). All four lost "static" here:
// building_interaction.cpp's update_mining_and_placement() needs external linkage to
// read/write them from another translation unit - same pattern as g_prev_lmb above.
bool g_has_place_target = false;
int g_place_x = 0;
int g_place_y = 0;
bool g_place_in_range = false;

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
// setup_opengl() moved to win32_platform.h/.cpp (verbatim, stays static there) - the
// win32_platform extraction stage. It is only ever called from WinMain (also moved there),
// so it needed no declaration anywhere else.

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

// Fog manual (raylib/rlgl nao tem equivalente a glFog*): aplica o lerp de cor em direcao a
// g_frame_fog (setado uma vez por frame em render_world(), ver render_primitives.h) baseado
// na distancia da camera ate a posicao dada - mesma formula do fog GL_LINEAR original,
// usada como aproximacao por quad/cubo (nao por vertice) ja que essas funcoes locais so
// recebem uma posicao "centro" por chamada. Compartilhada por render_cube_3d_tex/
// render_plane_3d/render_plane_3d_tex abaixo.
static void apply_frame_fog_local(float wx, float wy, float wz, float& r, float& g, float& b) {
    if (!g_frame_fog.enabled) return;
    float dx = wx - g_camera.position.x, dy = wy - g_camera.position.y, dz = wz - g_camera.position.z;
    float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    float span = std::max(0.0001f, g_frame_fog.end - g_frame_fog.start);
    float factor = clamp01((g_frame_fog.end - dist) / span);
    r = lerp(g_frame_fog.r, r, factor);
    g = lerp(g_frame_fog.g, g, factor);
    b = lerp(g_frame_fog.b, b, factor);
}

// Renderizar cubo 3D texturizado (tile do atlas) com iluminacao fake por face.
// Requer rlSetTexture(g_tex_atlas) ativo (equivalente ao antigo glBindTexture).
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

    // Fog manual (rlgl nao tem glFog*): aplicado uma vez usando o centro do cubo como
    // aproximacao de distancia (ver render_primitives.h's FrameFogParams).
    apply_frame_fog_local(x, y, z, tint_r, tint_g, tint_b);

    rlBegin(RL_QUADS);

    // Top (Y+)
    rlColor4f(tint_r * top_shade, tint_g * top_shade, tint_b * top_shade, a);
    rlTexCoord2f(uv_top.u0, uv_top.v1); rlVertex3f(x - half, y + half, z - half);
    rlTexCoord2f(uv_top.u1, uv_top.v1); rlVertex3f(x + half, y + half, z - half);
    rlTexCoord2f(uv_top.u1, uv_top.v0); rlVertex3f(x + half, y + half, z + half);
    rlTexCoord2f(uv_top.u0, uv_top.v0); rlVertex3f(x - half, y + half, z + half);

    // Bottom (Y-)
    rlColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
    rlTexCoord2f(uv_bottom.u0, uv_bottom.v0); rlVertex3f(x - half, y - half, z + half);
    rlTexCoord2f(uv_bottom.u1, uv_bottom.v0); rlVertex3f(x + half, y - half, z + half);
    rlTexCoord2f(uv_bottom.u1, uv_bottom.v1); rlVertex3f(x + half, y - half, z - half);
    rlTexCoord2f(uv_bottom.u0, uv_bottom.v1); rlVertex3f(x - half, y - half, z - half);

    // Front (Z+)
    rlColor4f(tint_r * side_shade, tint_g * side_shade, tint_b * side_shade, a);
    rlTexCoord2f(uv_side.u0, uv_side.v0); rlVertex3f(x - half, y - half, z + half);
    rlTexCoord2f(uv_side.u1, uv_side.v0); rlVertex3f(x + half, y - half, z + half);
    rlTexCoord2f(uv_side.u1, uv_side.v1); rlVertex3f(x + half, y + half, z + half);
    rlTexCoord2f(uv_side.u0, uv_side.v1); rlVertex3f(x - half, y + half, z + half);

    // Back (Z-)
    rlColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
    rlTexCoord2f(uv_side.u0, uv_side.v0); rlVertex3f(x + half, y - half, z - half);
    rlTexCoord2f(uv_side.u1, uv_side.v0); rlVertex3f(x - half, y - half, z - half);
    rlTexCoord2f(uv_side.u1, uv_side.v1); rlVertex3f(x - half, y + half, z - half);
    rlTexCoord2f(uv_side.u0, uv_side.v1); rlVertex3f(x + half, y + half, z - half);

    // Left (X-)
    rlColor4f(tint_r * dark_shade, tint_g * dark_shade, tint_b * dark_shade, a);
    rlTexCoord2f(uv_side.u0, uv_side.v0); rlVertex3f(x - half, y - half, z - half);
    rlTexCoord2f(uv_side.u1, uv_side.v0); rlVertex3f(x - half, y - half, z + half);
    rlTexCoord2f(uv_side.u1, uv_side.v1); rlVertex3f(x - half, y + half, z + half);
    rlTexCoord2f(uv_side.u0, uv_side.v1); rlVertex3f(x - half, y + half, z - half);

    // Right (X+)
    rlColor4f(tint_r * side_shade, tint_g * side_shade, tint_b * side_shade, a);
    rlTexCoord2f(uv_side.u0, uv_side.v0); rlVertex3f(x + half, y - half, z + half);
    rlTexCoord2f(uv_side.u1, uv_side.v0); rlVertex3f(x + half, y - half, z - half);
    rlTexCoord2f(uv_side.u1, uv_side.v1); rlVertex3f(x + half, y + half, z - half);
    rlTexCoord2f(uv_side.u0, uv_side.v1); rlVertex3f(x + half, y + half, z + half);

    rlEnd();

    if (outline) {
        render_cube_outline_3d(x, y, z, size, 1.0f);
        // render_cube_outline_3d() draws via DrawCubeWiresV -> rlBegin(RL_LINES). rlgl's
        // rlBegin() (rlgl.h) unconditionally resets the NEW draw call's textureId to
        // RLGL.State.defaultTextureId (a hardcoded opaque-white 1x1 texture) whenever the
        // primitive mode changes - it has no memory of whatever texture the app bound via
        // rlSetTexture(). render_world() only calls rlSetTexture(g_tex_atlas) once, before the
        // whole terrain loop starts, so without re-asserting it here, the very next RL_QUADS
        // draw (any later tile's plane/wall/cube in this same frame) silently samples the blank
        // white default texture instead of the atlas for the rest of the frame - this was the
        // root cause of "flat white/gray terrain, zero color variation between block types"
        // (this function is only reached when use_textures is true, so g_tex_atlas is valid).
        rlSetTexture(g_tex_atlas);
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
    apply_frame_fog_local(x, y, z, r, g, b);
    rlColor4f(r, g, b, a);
    rlBegin(RL_QUADS);
    rlVertex3f(x - half, y, z - half);
    rlVertex3f(x + half, y, z - half);
    rlVertex3f(x + half, y, z + half);
    rlVertex3f(x - half, y, z + half);
    rlEnd();
}

// Renderizar plano texturizado (tile do atlas). Requer rlSetTexture(g_tex_atlas) ativo.
static void render_plane_3d_tex(float x, float y, float z, float size, Tile tile,
                                float tint_r, float tint_g, float tint_b, float a = 1.0f) {
    float half = size * 0.5f;
    UvRect uv = atlas_uv(tile);
    apply_frame_fog_local(x, y, z, tint_r, tint_g, tint_b);
    rlColor4f(tint_r, tint_g, tint_b, a);
    rlBegin(RL_QUADS);
    rlTexCoord2f(uv.u0, uv.v0); rlVertex3f(x - half, y, z - half);
    rlTexCoord2f(uv.u1, uv.v0); rlVertex3f(x + half, y, z - half);
    rlTexCoord2f(uv.u1, uv.v1); rlVertex3f(x + half, y, z + half);
    rlTexCoord2f(uv.u0, uv.v1); rlVertex3f(x - half, y, z + half);
    rlEnd();
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

    // Corpo do cilindro (GL_QUAD_STRIP -> RL_QUADS: buffer do par de vertices anterior,
    // emite o quad (prev_top,prev_bot,cur_bot,cur_top) a partir da 2a iteracao).
    rlBegin(RL_QUADS);
    float prev_tx = 0, prev_ty = 0, prev_tz = 0, prev_r = 0, prev_g = 0, prev_b = 0;
    float prev_bx = 0, prev_by = 0, prev_bz = 0;
    bool have_prev = false;
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * kPi * (float)i / segments;
        float x = std::cos(angle);
        float z = std::sin(angle);
        float shade = 0.7f + 0.3f * std::fabs(x);  // Sombreamento lateral
        float cr = r * shade, cg = g * shade, cb = b * shade;
        float tx = cx + radius * x, ty = cy + half_h, tz = cz + radius * z;
        float bx = cx + radius * x, by = cy - half_h, bz = cz + radius * z;

        if (have_prev) {
            rlColor4f(prev_r, prev_g, prev_b, a); rlVertex3f(prev_tx, prev_ty, prev_tz);
            rlColor4f(prev_r, prev_g, prev_b, a); rlVertex3f(prev_bx, prev_by, prev_bz);
            rlColor4f(cr, cg, cb, a); rlVertex3f(bx, by, bz);
            rlColor4f(cr, cg, cb, a); rlVertex3f(tx, ty, tz);
        }
        prev_tx = tx; prev_ty = ty; prev_tz = tz;
        prev_bx = bx; prev_by = by; prev_bz = bz;
        prev_r = cr; prev_g = cg; prev_b = cb;
        have_prev = true;
    }
    rlEnd();

    // Topo (GL_TRIANGLE_FAN -> RL_TRIANGLES: buffer os vertices da fan, re-emite como
    // triplas (centro, v[i], v[i+1])).
    std::vector<Vector3> rim(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        float angle = 2.0f * kPi * (float)i / segments;
        rim[i] = {cx + radius * std::cos(angle), cy + half_h, cz + radius * std::sin(angle)};
    }
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < segments; ++i) {
        rlColor4f(r, g, b, a);
        rlVertex3f(cx, cy + half_h, cz);
        rlVertex3f(rim[i].x, rim[i].y, rim[i].z);
        rlVertex3f(rim[i + 1].x, rim[i + 1].y, rim[i + 1].z);
    }
    rlEnd();
}

static void render_physics_debug_3d() {
    if (!g_debug) return;

    Vec2 rp = get_player_render_pos();
    float ry = get_player_render_y();
    float hw = g_player.w * 0.5f;
    float hd = g_player.h * 0.5f;
    float foot = ry + g_physics_cfg.collision_skin;
    float head = foot + g_physics_cfg.collider_height;

    rlSetTexture(0);
    rlSetLineWidth(1.8f);

    // Collider AABB (GL_LINE_LOOP -> RL_LINES: cada par consecutivo + segmento de fechamento).
    rlBegin(RL_LINES);
    rlColor4f(0.10f, 0.95f, 1.0f, 0.95f);
    rlVertex3f(rp.x - hw, foot, rp.y - hd); rlVertex3f(rp.x + hw, foot, rp.y - hd);
    rlVertex3f(rp.x + hw, foot, rp.y - hd); rlVertex3f(rp.x + hw, foot, rp.y + hd);
    rlVertex3f(rp.x + hw, foot, rp.y + hd); rlVertex3f(rp.x - hw, foot, rp.y + hd);
    rlVertex3f(rp.x - hw, foot, rp.y + hd); rlVertex3f(rp.x - hw, foot, rp.y - hd);
    rlEnd();

    rlBegin(RL_LINES);
    rlColor4f(0.10f, 0.95f, 1.0f, 0.95f);
    rlVertex3f(rp.x - hw, head, rp.y - hd); rlVertex3f(rp.x + hw, head, rp.y - hd);
    rlVertex3f(rp.x + hw, head, rp.y - hd); rlVertex3f(rp.x + hw, head, rp.y + hd);
    rlVertex3f(rp.x + hw, head, rp.y + hd); rlVertex3f(rp.x - hw, head, rp.y + hd);
    rlVertex3f(rp.x - hw, head, rp.y + hd); rlVertex3f(rp.x - hw, head, rp.y - hd);
    rlEnd();

    rlBegin(RL_LINES);
    rlColor4f(0.10f, 0.95f, 1.0f, 0.95f);
    rlVertex3f(rp.x - hw, foot, rp.y - hd); rlVertex3f(rp.x - hw, head, rp.y - hd);
    rlVertex3f(rp.x + hw, foot, rp.y - hd); rlVertex3f(rp.x + hw, head, rp.y - hd);
    rlVertex3f(rp.x + hw, foot, rp.y + hd); rlVertex3f(rp.x + hw, head, rp.y + hd);
    rlVertex3f(rp.x - hw, foot, rp.y + hd); rlVertex3f(rp.x - hw, head, rp.y + hd);
    rlEnd();

    // Ground rays.
    for (int i = 0; i < g_physics.debug_ray_count; ++i) {
        const PhysicsRayDebug& ray = g_physics.debug_rays[(size_t)i];
        rlBegin(RL_LINES);
        if (ray.hit) rlColor4f(0.20f, 1.0f, 0.30f, 0.90f);
        else rlColor4f(1.0f, 0.20f, 0.20f, 0.90f);
        rlVertex3f(ray.from.x, ray.from.y, ray.from.z);
        rlVertex3f(ray.to.x, ray.to.y, ray.to.z);
        rlEnd();
    }

    // Camera rays (obstruction checks).
    for (int i = 0; i < g_camera_debug_ray_count; ++i) {
        const CameraDebugRay& ray = g_camera_debug_rays[(size_t)i];
        rlBegin(RL_LINES);
        if (ray.blocked) rlColor4f(1.0f, 0.35f, 0.20f, 0.92f);
        else rlColor4f(0.35f, 0.78f, 1.0f, 0.85f);
        rlVertex3f(ray.from.x, ray.from.y, ray.from.z);
        rlVertex3f(ray.to.x, ray.to.y, ray.to.z);
        rlEnd();
    }

    // Ground normal.
    Vec3 n0 = {rp.x, g_player.ground_height + 0.03f, rp.y};
    Vec3 n1 = {n0.x + g_physics.ground_normal.x * 1.1f,
               n0.y + g_physics.ground_normal.y * 1.1f,
               n0.z + g_physics.ground_normal.z * 1.1f};
    rlBegin(RL_LINES);
    rlColor4f(0.30f, 0.70f, 1.0f, 1.0f);
    rlVertex3f(n0.x, n0.y, n0.z);
    rlVertex3f(n1.x, n1.y, n1.z);
    rlEnd();

    // Velocity vector.
    Vec3 v0 = {rp.x, ry + 0.90f, rp.y};
    Vec3 v1 = {
        v0.x + g_player.vel.x * 0.20f,
        v0.y + g_player.vel_y * 0.10f,
        v0.z + g_player.vel.y * 0.20f
    };
    rlBegin(RL_LINES);
    rlColor4f(1.0f, 0.85f, 0.25f, 1.0f);
    rlVertex3f(v0.x, v0.y, v0.z);
    rlVertex3f(v1.x, v1.y, v1.z);
    rlEnd();

    // Collision normal.
    if (g_physics.hit_x || g_physics.hit_z) {
        Vec3 c0 = {rp.x, foot + 0.15f, rp.y};
        Vec3 c1 = {c0.x + g_physics.collision_normal.x * 0.7f, c0.y, c0.z + g_physics.collision_normal.y * 0.7f};
        rlBegin(RL_LINES);
        rlColor4f(1.0f, 0.2f, 1.0f, 1.0f);
        rlVertex3f(c0.x, c0.y, c0.z);
        rlVertex3f(c1.x, c1.y, c1.z);
        rlEnd();
    }

    rlSetLineWidth(1.0f);
}

// ============= Rendering =============
// render_world() loses "static" here for the same reason update_game() does just below in
// this file: win32_platform.cpp's WinMain() (the win32_platform extraction stage) calls it
// from another translation unit now, via its own forward declaration.
void render_world(int win_w, int win_h) {
    if (!g_world) return;

    // === SETUP 3D ===
    rlViewport(0, 0, win_w, win_h);
    rlEnableDepthTest(); // GL_LESS e o depth func padrao da raylib/rlgl, nao precisa setar.
    // IMPORTANTE: rlglInit() (chamado dentro de InitWindow) habilita GL_CULL_FACE por padrao
    // (glCullFace(GL_BACK)/glFrontFace(GL_CCW)/glEnable(GL_CULL_FACE) - ver rlgl.h). O jogo
    // original (OpenGL 1.x fixo) NUNCA usava GL_CULL_FACE em lugar nenhum - toda a geometria
    // manual deste arquivo/render_primitives.cpp/sky.cpp (glVertex3f/rlVertex3f por face) foi
    // desenhada sem se preocupar com winding/orientacao consistente, contando com o default
    // real do OpenGL (culling desabilitado). Sem este disable, ~metade das faces manuais
    // (chao/paredes do terreno, cubos do player e dos modulos da base, dome do ceu) somem
    // dependendo do angulo de camera, porque o winding delas nunca foi pensado para culling.
    rlDisableBackfaceCulling();
    rlClearColor(13, 15, 20, 255); // 0.05,0.06,0.08 * 255 - cor de fundo inicial (antes do ceu)
    rlClearScreenBuffers();

    // Projecao perspectiva
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    float aspect = (float)win_w / (float)win_h;
    apply_perspective(74.0f, aspect, 0.1f, 2200.0f);

    // Atualizar camera (target + colisao) para o frame atual
    update_camera_for_frame();

    // Aplicar view matrix
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    apply_look_at();

    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float atmos_factor = clamp01(g_atmosphere / 100.0f);
    SkyPalette sky_palette = compute_sky_palette(day_phase, atmos_factor);
    float sky_r = lerp(sky_palette.hz_r, sky_palette.zn_r, 0.35f);
    float sky_g = lerp(sky_palette.hz_g, sky_palette.zn_g, 0.35f);
    float sky_b = lerp(sky_palette.hz_b, sky_palette.zn_b, 0.35f);

    // Clear com cor do ceu alienigena
    rlClearColor((unsigned char)std::clamp((int)(sky_r * 255.0f), 0, 255),
                 (unsigned char)std::clamp((int)(sky_g * 255.0f), 0, 255),
                 (unsigned char)std::clamp((int)(sky_b * 255.0f), 0, 255), 255);
    rlClearScreenBuffers();

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

        float fog_start = std::max(70.0f, (float)view_radius * g_sky_cfg.fog_start_factor * fog_start_mul);
        float fog_end = std::max(fog_start + 110.0f,
                                 (float)view_radius * g_sky_cfg.fog_end_factor * fog_end_mul + g_sky_cfg.fog_distance_bonus);

        // Fog manual (rlgl/raylib nao tem glFog*): parametros setados uma vez aqui, lidos por
        // render_cube_3d()/render_wall_3d_tex() (render_primitives.cpp) e pelas funcoes locais
        // render_plane_3d()/render_plane_3d_tex()/render_cube_3d_tex() (acima, neste arquivo)
        // via g_frame_fog (ver render_primitives.h) - mesmos valores de cor/inicio/fim que os
        // antigos glFogfv/glFogf usavam.
        g_frame_fog.enabled = true;
        g_frame_fog.start = fog_start;
        g_frame_fog.end = fog_end;
        g_frame_fog.r = fog_col[0];
        g_frame_fog.g = fog_col[1];
        g_frame_fog.b = fog_col[2];
    }

    // === RENDERIZACAO 3D DO MUNDO ===
    
    int start_x = std::max(0, player_tile_x - view_radius);
    int end_x = std::min(g_world->w - 1, player_tile_x + view_radius);
    int start_z = std::max(0, player_tile_z - view_radius);
    int end_z = std::min(g_world->h - 1, player_tile_z + view_radius);
    
    // Texturas
    bool use_textures = (g_tex_atlas != 0);
    if (use_textures) {
        rlSetTexture(g_tex_atlas);
    } else {
        rlSetTexture(0);
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
                                float wr = tint_r * s, wg = tint_g * s, wb = tint_b * s;
                                apply_frame_fog_local(world_x, (h_here) , world_z, wr, wg, wb);
                                rlColor4f(wr, wg, wb, a);
                            };
                            constexpr float half = 0.5f;
                            if (h_e < h_here) {
                                rlBegin(RL_QUADS);
                                wall_col(side_shade);
                                rlVertex3f(world_x + half, h_e, world_z - half);
                                rlVertex3f(world_x + half, h_e, world_z + half);
                                rlVertex3f(world_x + half, h_here, world_z + half);
                                rlVertex3f(world_x + half, h_here, world_z - half);
                                rlEnd();
                            }
                            if (h_w < h_here) {
                                rlBegin(RL_QUADS);
                                wall_col(dark_shade);
                                rlVertex3f(world_x - half, h_w, world_z + half);
                                rlVertex3f(world_x - half, h_w, world_z - half);
                                rlVertex3f(world_x - half, h_here, world_z - half);
                                rlVertex3f(world_x - half, h_here, world_z + half);
                                rlEnd();
                            }
                            if (h_s < h_here) {
                                rlBegin(RL_QUADS);
                                wall_col(side_shade);
                                rlVertex3f(world_x - half, h_s, world_z + half);
                                rlVertex3f(world_x + half, h_s, world_z + half);
                                rlVertex3f(world_x + half, h_here, world_z + half);
                                rlVertex3f(world_x - half, h_here, world_z + half);
                                rlEnd();
                            }
                            if (h_n < h_here) {
                                rlBegin(RL_QUADS);
                                wall_col(dark_shade);
                                rlVertex3f(world_x + half, h_n, world_z - half);
                                rlVertex3f(world_x - half, h_n, world_z - half);
                                rlVertex3f(world_x - half, h_here, world_z - half);
                                rlVertex3f(world_x + half, h_here, world_z - half);
                                rlEnd();
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
        rlSetTexture(0);
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
        rlDisableDepthTest();
        render_plane_3d(px, g_player.ground_height + 0.02f, pz, 0.9f, 0.0f, 0.0f, 0.0f, 0.55f);

        // Circulo de indicador de perigo
        if (in_danger) {
            render_plane_3d(px, g_player.ground_height + 0.03f, pz, 1.2f,
                kColorDanger[0], kColorDanger[1], kColorDanger[2], danger_pulse * 0.3f);
        }
        rlEnableDepthTest();
        
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
        rlDisableDepthTest();
        render_plane_3d(lamp_x + sin_rot * 0.18f, py + 0.70f + bob, lamp_z + cos_rot * 0.18f, 0.42f, 1.0f, 0.92f, 0.68f, 0.20f * lamp_i);
        rlEnableDepthTest();
        
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
        rlSetLineWidth(lw);
        rlBegin(RL_LINES);
        rlColor4f(r, g, b, a);
        rlVertex3f((float)tx - half, y, (float)tz - half); rlVertex3f((float)tx + half, y, (float)tz - half);
        rlVertex3f((float)tx + half, y, (float)tz - half); rlVertex3f((float)tx + half, y, (float)tz + half);
        rlVertex3f((float)tx + half, y, (float)tz + half); rlVertex3f((float)tx - half, y, (float)tz + half);
        rlVertex3f((float)tx - half, y, (float)tz + half); rlVertex3f((float)tx - half, y, (float)tz - half);
        rlEnd();
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

        rlSetTexture(0);
        rlSetBlendMode(RL_BLEND_ALPHA);

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
                rlSetLineWidth(1.5f);
                float half = 1.04f * 0.5f;
                rlBegin(RL_LINES);
                rlColor4f(1.0f, 1.0f, 1.0f, 0.55f);
                rlVertex3f((float)g_target_x - half, cy + half, (float)g_target_y - half); rlVertex3f((float)g_target_x + half, cy + half, (float)g_target_y - half);
                rlVertex3f((float)g_target_x + half, cy + half, (float)g_target_y - half); rlVertex3f((float)g_target_x + half, cy + half, (float)g_target_y + half);
                rlVertex3f((float)g_target_x + half, cy + half, (float)g_target_y + half); rlVertex3f((float)g_target_x - half, cy + half, (float)g_target_y + half);
                rlVertex3f((float)g_target_x - half, cy + half, (float)g_target_y + half); rlVertex3f((float)g_target_x - half, cy + half, (float)g_target_y - half);
                rlEnd();
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

            rlDisableDepthMask();
            rlSetTexture(g_tex_atlas);
            render_plane_3d_tex(target_x, crack_y, target_z, 1.04f, crack, 1.0f, 1.0f, 1.0f, 1.0f);
            rlSetTexture(0);
            rlEnableDepthMask();

            // Barra de quebra opcional (feedback claro).
            if (!is_player_tile(g_target_x, g_target_y)) {
                float bar_w = 0.84f;
                float bar_h = 0.06f;
                float y = crack_y + 0.012f;
                float x0 = (float)g_target_x - bar_w * 0.5f;
                float z0 = (float)g_target_y - 0.56f;
                rlSetTexture(0);
                rlBegin(RL_QUADS);
                rlColor4f(0.02f, 0.02f, 0.03f, 0.78f);
                rlVertex3f(x0, y, z0);
                rlVertex3f(x0 + bar_w, y, z0);
                rlVertex3f(x0 + bar_w, y, z0 + bar_h);
                rlVertex3f(x0, y, z0 + bar_h);
                rlEnd();

                float fill = std::clamp(g_mine_progress, 0.0f, 1.0f) * (bar_w - 0.02f);
                rlBegin(RL_QUADS);
                rlColor4f(0.95f, 0.82f, 0.26f, 0.92f);
                rlVertex3f(x0 + 0.01f, y + 0.001f, z0 + 0.01f);
                rlVertex3f(x0 + 0.01f + fill, y + 0.001f, z0 + 0.01f);
                rlVertex3f(x0 + 0.01f + fill, y + 0.001f, z0 + bar_h - 0.01f);
                rlVertex3f(x0 + 0.01f, y + 0.001f, z0 + bar_h - 0.01f);
                rlEnd();
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
            
            rlSetBlendMode(RL_BLEND_ALPHA);
            rlSetTexture(0);
            rlDisableDepthMask();

            // Pilar de luz principal (gradiente vertical). GL_QUAD_STRIP -> RL_QUADS: buffer
            // do par de vertices anterior, emite (prev_left,prev_right,cur_right,cur_left) a
            // partir da 2a iteracao.
            {
                rlBegin(RL_QUADS);
                float prev_lx = 0, prev_ly = 0, prev_lz = 0, prev_rx = 0, prev_ry = 0, prev_rz = 0, prev_a = 0;
                bool have_prev = false;
                for (int i = 0; i <= 20; ++i) {
                    float t = (float)i / 20.0f;
                    float y = base_h + t * (beacon_h - base_h);
                    float alpha = (1.0f - t * 0.8f) * final_alpha;
                    float width = 0.2f * (1.0f - t * 0.5f);  // Afina no topo

                    float lx = bx - width, ly = y, lz = bz;
                    float rx = bx + width, ry = y, rz = bz;
                    if (have_prev) {
                        rlColor4f(0.3f, 0.8f, 1.0f, prev_a); rlVertex3f(prev_lx, prev_ly, prev_lz);
                        rlColor4f(0.3f, 0.8f, 1.0f, prev_a); rlVertex3f(prev_rx, prev_ry, prev_rz);
                        rlColor4f(0.3f, 0.8f, 1.0f, alpha); rlVertex3f(rx, ry, rz);
                        rlColor4f(0.3f, 0.8f, 1.0f, alpha); rlVertex3f(lx, ly, lz);
                    }
                    prev_lx = lx; prev_ly = ly; prev_lz = lz;
                    prev_rx = rx; prev_ry = ry; prev_rz = rz;
                    prev_a = alpha;
                    have_prev = true;
                }
                rlEnd();
            }

            // Pilar secundario (perpendicular para visibilidade 3D)
            {
                rlBegin(RL_QUADS);
                float prev_lx = 0, prev_ly = 0, prev_lz = 0, prev_rx = 0, prev_ry = 0, prev_rz = 0, prev_a = 0;
                bool have_prev = false;
                for (int i = 0; i <= 20; ++i) {
                    float t = (float)i / 20.0f;
                    float y = base_h + t * (beacon_h - base_h);
                    float alpha = (1.0f - t * 0.8f) * final_alpha * 0.7f;
                    float width = 0.15f * (1.0f - t * 0.5f);

                    float lx = bx, ly = y, lz = bz - width;
                    float rx = bx, ry = y, rz = bz + width;
                    if (have_prev) {
                        rlColor4f(0.3f, 0.8f, 1.0f, prev_a); rlVertex3f(prev_lx, prev_ly, prev_lz);
                        rlColor4f(0.3f, 0.8f, 1.0f, prev_a); rlVertex3f(prev_rx, prev_ry, prev_rz);
                        rlColor4f(0.3f, 0.8f, 1.0f, alpha); rlVertex3f(rx, ry, rz);
                        rlColor4f(0.3f, 0.8f, 1.0f, alpha); rlVertex3f(lx, ly, lz);
                    }
                    prev_lx = lx; prev_ly = ly; prev_lz = lz;
                    prev_rx = rx; prev_ry = ry; prev_rz = rz;
                    prev_a = alpha;
                    have_prev = true;
                }
                rlEnd();
            }

            // Halo na base do beacon (GL_TRIANGLE_FAN -> RL_TRIANGLES: buffer o anel, re-emite
            // como triplas (centro, v[i], v[i+1])).
            {
                float halo_pulse = 0.6f + 0.4f * pulse;
                float center_alpha = final_alpha * 0.5f * halo_pulse;
                std::vector<Vector3> rim(17);
                for (int i = 0; i <= 16; ++i) {
                    float angle = (float)i * (2.0f * kPi / 16.0f);
                    float halo_r = 1.5f * halo_pulse;
                    rim[i] = {bx + std::cos(angle) * halo_r, base_h + 0.05f, bz + std::sin(angle) * halo_r};
                }
                rlBegin(RL_TRIANGLES);
                for (int i = 0; i < 16; ++i) {
                    rlColor4f(0.3f, 0.8f, 1.0f, center_alpha);
                    rlVertex3f(bx, base_h + 0.1f, bz);
                    rlColor4f(0.3f, 0.8f, 1.0f, 0.0f);
                    rlVertex3f(rim[i].x, rim[i].y, rim[i].z);
                    rlColor4f(0.3f, 0.8f, 1.0f, 0.0f);
                    rlVertex3f(rim[i + 1].x, rim[i + 1].y, rim[i + 1].z);
                }
                rlEnd();
            }

            rlEnableDepthMask();
        }
    }
    
    render_hud(win_w, win_h);
    // Overlays - Menus estilo Minecraft (Paused/Menu/Dead/Settings). Extracted verbatim to
    // ui_menu.cpp's render_menus() - see ui_menu.h for details; the build menu (g_show_build_menu)
    // and the victory/alerts/world-map overlays right after it stay inline here.
    render_menus(win_w, win_h);

    // One-time victory celebration (fires when objectives.cpp completes the final
    // milestone) instead of the old permanent "if (g_victory)" overlay that never went
    // away once triggered - the objectives HUD panel (ui_hud.cpp) now shows a permanent
    // "Marte Terraformado!" line once all milestones are done, so this overlay only needs
    // to cover the initial celebratory moment.
    float victory_celebration = objectives_victory_celebration_remaining();
    if (victory_celebration > 0.0f) {
        float alpha = std::min(1.0f, victory_celebration / 2.0f);  // fade out over the last 2s
        render_quad(0.0f, 0.0f, (float)win_w, (float)win_h, 0.0f, 0.0f, 0.0f, 0.30f * alpha);
        std::string t1 = "Marte Terraformado!";
        std::string t2 = "Parabens, colono - voce completou todos os objetivos.";
        draw_text(win_w * 0.5f - estimate_text_w_px(t1) * 0.5f, win_h * 0.20f, t1, 0.85f, 0.95f, 0.85f, 0.98f * alpha);
        draw_text(win_w * 0.5f - estimate_text_w_px(t2) * 0.5f, win_h * 0.20f + 26.0f, t2, 0.80f, 0.90f, 0.80f, 0.90f * alpha);
    }
    
    // ============= BUILD MENU =============
    // Extracted verbatim to building_interaction.cpp's render_build_menu() - see
    // building_interaction.h for details. The guard that used to wrap this block
    // ("if (g_show_build_menu && g_state == GameState::Playing)") now lives inside that
    // function instead (same pattern as render_menus() above checking g_state internally).
    render_build_menu(win_w, win_h);

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

    // SwapBuffers(hdc) removed: win32_platform.cpp's main loop wraps this call in
    // BeginDrawing()/EndDrawing(), which handles the buffer swap now.
}

// ============= Input State =============
// key_down()/key_pressed() moved to input.h/.cpp (verbatim) - the input extraction stage.
// input.h (included at the top of this file) supplies both declarations; update_game()
// below still calls key_pressed() exactly as before, for every g_prev_<key> debounce
// global, all of which stay right here (they are this function's own hotkey-polling state,
// not part of the input module - see input.h for the full reasoning).

// ============= Update =============
// update_game() loses "static" here: win32_platform.cpp's WinMain() (the win32_platform
// extraction stage, the last of this whole refactor) calls it from another translation
// unit now, via its own forward declaration (no header owns render_world()/update_game()
// themselves, since they are the two intentional final orchestrators left in this file,
// not a reusable module) - same pattern as every other "lost static" function in this
// codebase's extraction stages.
void update_game(float dt) {
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
    bool esc_pressed = key_pressed(KEY_ESCAPE, g_prev_esc);
    bool enter_pressed = key_pressed(KEY_ENTER, g_prev_enter);
    bool f5_pressed = key_pressed(KEY_F5, g_prev_f5);
    bool f9_pressed = key_pressed(KEY_F9, g_prev_f9);
    bool l_pressed = key_pressed(KEY_L, g_prev_l);
    bool q_pressed = key_pressed(KEY_Q, g_prev_q);
    bool f3_pressed = key_pressed(KEY_F3, g_prev_f3);
    bool f6_pressed = key_pressed(KEY_F6, g_prev_f6);
    bool f7_pressed = key_pressed(KEY_F7, g_prev_f7);
    bool h_pressed = key_pressed(KEY_H, g_prev_h);
    bool tab_pressed = key_pressed(KEY_TAB, g_prev_tab);
    bool b_pressed = key_pressed(KEY_B, g_prev_b);
    bool m_pressed = key_pressed(KEY_M, g_prev_m);
    bool r_pressed = key_pressed(KEY_R, g_prev_r);
    bool c_key_pressed = key_pressed(KEY_C, g_prev_c);
    
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
        if (key_down(KEY_W) || key_down(KEY_UP)) g_minimap.world_pan_y -= pan_speed;
        if (key_down(KEY_S) || key_down(KEY_DOWN)) g_minimap.world_pan_y += pan_speed;
        if (key_down(KEY_A) || key_down(KEY_LEFT)) g_minimap.world_pan_x -= pan_speed;
        if (key_down(KEY_D) || key_down(KEY_RIGHT)) g_minimap.world_pan_x += pan_speed;

        // Limitar pan aos limites do mundo
        g_minimap.world_pan_x = std::clamp(g_minimap.world_pan_x, 0.0f, (float)g_world->w);
        g_minimap.world_pan_y = std::clamp(g_minimap.world_pan_y, 0.0f, (float)g_world->h);

        // Clique para adicionar waypoint
        if (g_mouse_left_clicked) {
            // Converter posicao do mouse para coordenadas do mundo (raylib: sem HWND, o
            // tamanho da janela vem direto de GetScreenWidth/Height)
            int win_w = GetScreenWidth();
            int win_h = GetScreenHeight();

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

    // State machine - Menu/Paused/Settings/Dead input handling. Extracted verbatim to
    // ui_menu.cpp's update_menu_input() - see ui_menu.h for details; each original
    // "return;" became "return true;" (this frame's input was fully consumed by a menu
    // screen), with a final "return false;" added for the Playing state (no menu state
    // matched - fall through to this file's own Playing-state input code below).
    if (update_menu_input(dt, esc_pressed, enter_pressed, f5_pressed, f9_pressed, l_pressed, q_pressed)) return;

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
    
    // Build menu navigation and actions. Extracted verbatim to building_interaction.cpp's
    // update_build_menu_input() - see building_interaction.h for details; the original
    // unconditional "return;" (when g_show_build_menu was open) became "return true;", with
    // "return false;" added for when the menu isn't open (fall through to the rest of this
    // function) - same "return true consumes the frame" convention as
    // update_menu_input() above.
    if (update_build_menu_input()) return;

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
    if (key_down(KEY_W) || key_down(KEY_UP)) input_forward += 1.0f;
    if (key_down(KEY_S) || key_down(KEY_DOWN)) input_forward -= 1.0f;
    if (key_down(KEY_A) || key_down(KEY_LEFT)) input_right -= 1.0f;
    if (key_down(KEY_D) || key_down(KEY_RIGHT)) input_right += 1.0f;

    Vec2 move_world = {
        input_forward * cam_forward_x + input_right * cam_right_x,
        input_forward * cam_forward_z + input_right * cam_right_z
    };
    bool has_input = (move_world.x != 0.0f || move_world.y != 0.0f);
    if (has_input) move_world = vec2_normalize(move_world);

    // VK_SHIFT has no direct raylib equivalent (raylib splits left/right shift).
    bool run_key = key_down(KEY_LEFT_SHIFT) || key_down(KEY_RIGHT_SHIFT);
    bool jump_held = key_down(KEY_SPACE);
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

    if (key_down(KEY_KP_ADD) || key_down(KEY_EQUAL)) {
        g_camera.distance = std::max(g_camera.min_distance, g_camera.distance - 10.0f * dt);
    }
    if (key_down(KEY_KP_SUBTRACT) || key_down(KEY_MINUS)) {
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
            // Onboarding: dica para voltar a base (uma vez), depois dica para se tornar
            // independente da base (shown_low_oxygen - antes uma flag morta, nunca
            // disparada; agora tem um gatilho proprio distinto do de shown_return_to_base).
            if (!g_onboarding.shown_return_to_base) {
                show_tip("H para voltar a base e recarregar oxigenio", g_onboarding.shown_return_to_base);
            } else if (!g_onboarding.shown_low_oxygen) {
                show_tip("Construa um Gerador de Oxigenio para nao depender so da base", g_onboarding.shown_low_oxygen);
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

    // Mouse targeting + mining/placement raycast/actions + item pickup + particle
    // simulation. Extracted verbatim to building_interaction.cpp's
    // update_mining_and_placement() - see building_interaction.h for details. The five
    // local [&]-capturing lambdas that used to live in this block (placeable_tile/
    // blocks_raycast/ray_aabb_hit/ray_hits_tile/placeable_tile_for_place) are now named,
    // explicit-parameter functions there instead - the highest-value mechanical change of
    // this extraction stage, per the refactor plan.
    update_mining_and_placement(dt);
}

// ============= Window Procedure / WinMain =============
// g_last_mouse_x/g_last_mouse_y/g_mouse_captured, setup_opengl(), WindowProc(), and
// WinMain() all moved to win32_platform.h/.cpp (verbatim) - the win32_platform extraction
// stage, the LAST stage of this whole refactor. win32_platform.h (included at the top of
// this file) declares nothing (see there for why); WinMain calls render_world()/
// update_game() above via its own forward declarations instead. This is the end of
// main.cpp: nothing follows WinMain() in win32_platform.cpp either.
