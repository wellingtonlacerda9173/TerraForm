#pragma once

// ============= Building Interaction (Build Menu / Mining / Placement) =============
// Extracted verbatim from main.cpp - the "building_interaction" seam of the refactor plan,
// covering two areas of render_world()/update_game():
//
// 1) The build-menu PANEL rendering (original render_world() lines ~1735-1933: the
//    "if (g_show_build_menu && g_state == GameState::Playing) { ... }" block drawing the
//    buildable-module list with costs/affordability/unlock status, the base-status resource
//    bars, and the inventory summary at the bottom). The victory overlay just before this
//    block and the alerts-display overlay right after it are NOT part of this extraction -
//    they stay inline in main.cpp, untouched.
//
// 2) Build-menu INPUT + mining/placement raycast (original update_game() lines ~2167-2263
//    and ~2440-2899):
//    - update_build_menu_input(): W/S navigation between buildable module types, Enter to
//      start construction (checking can_afford/existing construction jobs/free build
//      slots). The ESC-closes-build-menu-or-pauses block and the Tab/B-toggles-build-menu
//      block that used to sit right before this in update_game() are GENERAL Playing-state
//      input dispatch, not specific to the build menu's own navigation, so they stay inline
//      in main.cpp (out of scope for this stage).
//    - update_mining_and_placement(): the mouse-targeting raycast (mining/placement), the
//      mining action (progress/hits/particles/block breaking/drops), item pickup
//      (update_item_drops), the placement action (RMB), and the particle simulation step.
//      This is the highest-value part of this extraction stage: five local lambdas that
//      used to capture their enclosing scope by reference ([&]) are now named, non-
//      capturing (or explicit-parameter) functions - placeable_tile/blocks_raycast/
//      ray_aabb_hit/ray_hits_tile/placeable_tile_for_place, all `static` (file-local) below
//      - so a later redesign of the construction/placement system (Fase 2 of the plan) has
//      real functions to call/test instead of inline capturing lambdas.
//
// render_build_menu()/update_build_menu_input()/update_mining_and_placement() lost "static"
// by construction: they are defined here and called from render_world()/update_game() in
// main.cpp, which is a different translation unit - same pattern as every other
// render_*()/update_*() entry point declared in the other extracted headers.
void render_build_menu(int win_w, int win_h);

// Returns true if g_show_build_menu was open and this frame's input was fully consumed by
// the build menu's own navigation (mirrors the original code's unconditional "return;" at
// the end of the "if (g_show_build_menu) { ... }" block) - update_game() should itself
// return immediately when this is true, exactly as before. Returns false when the build
// menu isn't open (g_show_build_menu is false), meaning update_game() should fall through
// to its own Playing-state input code below. Takes no parameters: grep-confirmed the
// original block never reads dt/hwnd, only g_show_build_menu/g_build_menu_selection/
// key_down() and the module/construction-queue/build-slot globals.
bool update_build_menu_input();

// Mouse targeting (mining/placement raycast), mining action, item pickup, placement action
// and particle simulation for one frame. Dropped the HWND parameter (raylib migration): the
// original code derived the mouse cursor position and window size from hwnd via
// GetCursorPos/ScreenToClient/GetClientRect - now GetMousePosition()/GetScreenWidth()/
// GetScreenHeight() supply the same information with no window handle needed.
void update_mining_and_placement(float dt);
