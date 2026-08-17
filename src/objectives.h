#pragma once

#include "blocks.h"

// ============= Player Objectives (milestone track) =============
// A single ordered sequence of milestones the player completes one at a time,
// reusing existing progression signals (module construction, TerraPhase) rather
// than introducing a parallel quest system. Once a milestone is completed it
// never un-completes, even if the underlying condition later stops being true
// (e.g. the module is destroyed, or the terraform score dips) - this matches
// how an "achievement" should behave, as opposed to a live status check.
enum class ObjectiveId {
    BuildFirstPower = 0,
    BuildWaterExtractor,
    BuildOxygenGenerator,
    BuildGreenhouse,
    BuildCO2Factory,
    ReachThawing,
    BuildHabitat,
    BuildTerraformerBeacon,
    ReachHabitable,
    TerraformComplete,
};
constexpr int kObjectiveCount = 10;

struct ObjectiveDef {
    const char* title;
    const char* hint;
    Block related_module;  // Block::Air if this milestone isn't gated by a specific module unlock
};

const ObjectiveDef& objective_def(int index);
int objectives_current_index();      // 0..kObjectiveCount; == kObjectiveCount means all complete
bool objectives_all_complete();

// Call whenever the player actually places a module (both the queued-construction
// path and the instant-placement path in building_interaction.cpp call this).
void notify_module_built(Block type);

// Call once per tick (from update_modules, alongside update_phase()).
void update_objectives(float dt);

// New game / respawn-to-fresh-state.
void reset_objectives();

// While > 0, a one-time victory celebration overlay should be shown; counts down
// with dt inside update_objectives(). Replaces the old permanent g_victory overlay.
float objectives_victory_celebration_remaining();

// ---- Save/load support ----
const bool* objectives_ever_built_snapshot();  // size kBlockTypeCount, read-only view for save_game
void objectives_load_state(int current_index, const bool* ever_built, int ever_built_count);
