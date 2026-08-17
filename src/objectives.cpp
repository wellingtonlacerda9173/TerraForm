#include "objectives.h"

#include "game_state.h"

#include <algorithm>
#include <array>

// g_terraform/g_victory/g_phase are still owned by main.cpp (already non-static there for
// other extracted modules, e.g. world.cpp) - same "own local extern" pattern used
// throughout this codebase's extraction stages.
extern float g_terraform;
extern bool g_victory;
extern TerraPhase g_phase;

static const ObjectiveDef kObjectives[kObjectiveCount] = {
    {"Gerar energia", "Construa um Painel Solar ou Gerador de Energia.", Block::SolarPanel},
    {"Extrair agua", "Construa um Extrator de Agua.", Block::WaterExtractor},
    {"Gerar oxigenio", "Construa um Gerador de Oxigenio.", Block::OxygenGenerator},
    {"Cultivar comida", "Construa uma Estufa.", Block::Greenhouse},
    {"Aquecer o planeta", "Construa uma Fabrica de CO2.", Block::CO2Factory},
    {"Descongelar Marte", "Aguarde a temperatura subir ate a fase Degelo.", Block::Air},
    {"Abrigar a colonia", "Construa um Habitat.", Block::Habitat},
    {"Verdejar o planeta", "Construa um Terraformer Beacon.", Block::TerraformerBeacon},
    {"Tornar Marte habitavel", "Aguarde temperatura e atmosfera alcancarem a fase Habitavel.", Block::Air},
    {"Terraformar Marte", "Continue espalhando vegetacao ate completar a terraformacao.", Block::Air},
};

static int g_current = 0;
static std::array<bool, kBlockTypeCount> g_ever_built = {};
static float g_victory_celebration = 0.0f;

const ObjectiveDef& objective_def(int index) {
    index = std::clamp(index, 0, kObjectiveCount - 1);
    return kObjectives[index];
}

int objectives_current_index() { return g_current; }
bool objectives_all_complete() { return g_current >= kObjectiveCount; }

void notify_module_built(Block type) {
    if (type == Block::Air) return;
    g_ever_built[(size_t)type] = true;
}

void reset_objectives() {
    g_current = 0;
    g_ever_built.fill(false);
    g_victory_celebration = 0.0f;
}

float objectives_victory_celebration_remaining() { return g_victory_celebration; }

const bool* objectives_ever_built_snapshot() { return g_ever_built.data(); }

void objectives_load_state(int current_index, const bool* ever_built, int ever_built_count) {
    g_current = std::clamp(current_index, 0, kObjectiveCount);
    g_ever_built.fill(false);
    if (ever_built) {
        int n = std::min(ever_built_count, (int)kBlockTypeCount);
        for (int i = 0; i < n; ++i) g_ever_built[(size_t)i] = ever_built[i];
    }
    g_victory_celebration = 0.0f;
}

static bool check_objective(int index) {
    switch ((ObjectiveId)index) {
        case ObjectiveId::BuildFirstPower:
            return g_ever_built[(size_t)Block::SolarPanel] || g_ever_built[(size_t)Block::EnergyGenerator];
        case ObjectiveId::BuildWaterExtractor:
            return g_ever_built[(size_t)Block::WaterExtractor];
        case ObjectiveId::BuildOxygenGenerator:
            return g_ever_built[(size_t)Block::OxygenGenerator];
        case ObjectiveId::BuildGreenhouse:
            return g_ever_built[(size_t)Block::Greenhouse];
        case ObjectiveId::BuildCO2Factory:
            return g_ever_built[(size_t)Block::CO2Factory];
        case ObjectiveId::ReachThawing:
            return (int)g_phase >= (int)TerraPhase::Thawing;
        case ObjectiveId::BuildHabitat:
            return g_ever_built[(size_t)Block::Habitat];
        case ObjectiveId::BuildTerraformerBeacon:
            return g_ever_built[(size_t)Block::TerraformerBeacon];
        case ObjectiveId::ReachHabitable:
            return (int)g_phase >= (int)TerraPhase::Habitable;
        case ObjectiveId::TerraformComplete:
            return g_phase == TerraPhase::Terraformed;
    }
    return false;
}

void update_objectives(float dt) {
    if (g_victory_celebration > 0.0f) {
        g_victory_celebration = std::max(0.0f, g_victory_celebration - dt);
    }

    if (g_current >= kObjectiveCount) return;
    if (!check_objective(g_current)) return;

    show_unlock_popup("Objetivo concluido!", kObjectives[g_current].title);
    ++g_current;

    if (g_current >= kObjectiveCount) {
        g_victory = true;
        g_victory_celebration = 8.0f;
        set_toast("Marte terraformado! Voce venceu!", 6.0f);
    }
}
