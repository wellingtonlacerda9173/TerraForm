#pragma once

#include "platform.h"   // std::string / std::vector / uint32_t (via <cstdint>)
#include "blocks.h"     // Block (used by CollectPopup / add_collect_popup)

// ============= Game State / Feedback / Onboarding =============
// Extracted verbatim from main.cpp (original lines ~60-65, 93-157, 1550-1659): the
// GameState enum + g_state, the toast/screen-flash/collect-popup/unlock-popup feedback
// globals and functions, the onboarding tip system, and the small xorshift32 RNG helpers
// used throughout gameplay/particle code.
//
// Alert, UnlockProgress and GameSettings struct DEFINITIONS also move here (they were
// textually interleaved with the feedback structs in main.cpp's global-state block), but
// their instances (g_alerts, g_unlocks, g_settings) stay defined in main.cpp: those
// globals are read/written extensively by alert/build/settings-menu logic elsewhere in
// main.cpp, not by this feedback subsystem, so moving the instances is out of scope for
// this stage.

// ---- Base alert entries (g_alerts vector stays owned by main.cpp) ----
struct Alert {
    std::string message;
    float r, g, b;
    float time_remaining;
};

// ---- Unlock progress (g_unlocks instance stays owned by main.cpp) ----
struct UnlockProgress {
    int total_stone = 0;
    int total_iron = 0;
    int total_coal = 0;
    int total_copper = 0;
    int total_wood = 0;

    bool solar_unlocked = false;
    bool water_extractor_unlocked = false;
    bool o2_generator_unlocked = false;
    bool greenhouse_unlocked = false;
    bool co2_factory_unlocked = false;
    bool habitat_unlocked = false;
    bool terraformer_unlocked = false;
};

// ---- Onboarding state (g_onboarding instance now owned by game_state.cpp) ----
struct OnboardingState {
    bool shown_first_move = false;
    bool shown_first_mine = false;
    bool shown_first_collect = false;
    bool shown_first_build_menu = false;
    bool shown_first_unlock = false;
    bool shown_return_to_base = false;
    bool shown_low_oxygen = false;
    bool shown_low_water = false;
    float tip_timer = 0.0f;        // Timer para mostrar dicas
    std::string current_tip = "";  // Dica atual
};

// ---- Accessibility settings (g_settings instance stays owned by main.cpp) ----
struct GameSettings {
    float ui_scale = 1.0f;           // 0.75 - 1.5
    float camera_sensitivity = 0.20f;
    bool invert_y = false;
    float brightness = 1.0f;
    float contrast = 1.0f;
};

// ---- Floating collect popup (g_collect_popups vector now owned by game_state.cpp) ----
struct CollectPopup {
    float x, y;
    Block item = Block::Air;
    int amount = 1;
    std::string text;
    float life;
    float r, g, b;
};

// ============= RNG =============
// Small xorshift32 PRNG used across gameplay/particle code. The seed/state (g_rng)
// stays `static` (internal) to game_state.cpp: nothing outside these two functions
// touches it directly.
uint32_t rng_next_u32();
float rng_next_f01();

// ============= Game state machine =============
enum class GameState {
    Playing = 0,
    Paused,
    Menu,
    Dead,      // Death screen
    Settings,  // Settings menu
};

// g_state is the single most-referenced global in main.cpp (menu/pause/dead/settings
// branching throughout update/render code), so it needs external linkage now that the
// enum and the variable live in this new translation unit.
extern GameState g_state;

// ============= Toast / feedback globals =============
// All of these are written by the feedback functions below (now defined in
// game_state.cpp) and also read by update/render code that stays in main.cpp, so they
// need external linkage - same pattern as g_oxygen/g_tex_atlas in earlier extraction
// stages (see textures.h/config_io.h).
extern float g_toast_time;
extern std::string g_toast;
extern float g_screen_flash_red;    // Flash vermelho (erro/dano)
extern float g_screen_flash_green;  // Flash verde (sucesso)
extern float g_hotbar_bounce;       // Animacao de bounce na hotbar
extern std::vector<CollectPopup> g_collect_popups;
extern float g_unlock_popup_timer;
extern std::string g_unlock_popup_text;
extern std::string g_unlock_popup_subtitle;
extern OnboardingState g_onboarding;

// NOTE: g_hotbar_bounce_slot (the slot index that bounce_hotbar_slot() sets) is NOT
// declared here on purpose - grep across the whole file shows it is only ever written
// by bounce_hotbar_slot(), never read anywhere (main.cpp's hotbar rendering only reacts
// to g_hotbar_bounce, not the slot index). Since nothing outside this subsystem touches
// it, it stays `static` (internal) inside game_state.cpp instead of being exposed here.

// ============= Feedback / onboarding functions =============
void set_toast(const std::string& msg, float seconds = 2.0f);
void show_error(const std::string& msg);
void show_success(const std::string& msg);
void add_collect_popup(float x, float y, const std::string& text, float r, float g, float b,
                        Block item = Block::Air, int amount = 1);
void show_unlock_popup(const std::string& title, const std::string& subtitle);
void bounce_hotbar_slot(int slot);
void show_tip(const std::string& tip, bool& shown_flag);
void update_onboarding(float dt);
