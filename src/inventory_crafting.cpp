#include "inventory_crafting.h"

#include <string>

// ============= Inventory / Crafting (state) =============
// O unico inventario/selecao do jogo. main.cpp perdeu o "static" que tinha nestas duas
// globais (elas ja eram nao-static la, pois player_physics.cpp's spawn_player_new_game()
// grants o starter kit / reseta o bloco selecionado); agora sao definidas aqui, dono
// natural do tipo CraftCost/da logica de inventario.
std::array<int, kBlockTypeCount> g_inventory = {};
Block g_selected = Block::Dirt;

// Module build costs (updated with new resources)
CraftCost get_module_cost(Block b) {
    CraftCost c{};
    switch (b) {
        case Block::SolarPanel:
            c.iron = 30; c.copper = 10;
            break;
        case Block::EnergyGenerator:
            c.iron = 40; c.crystal = 20; c.copper = 25;
            break;
        case Block::OxygenGenerator:
            c.ice = 50; c.iron = 50; c.copper = 20;
            break;
        case Block::WaterExtractor:
            c.ice = 30; c.metal = 20; c.copper = 15;
            break;
        case Block::Greenhouse:
            c.organic = 40; c.iron = 25; c.ice = 25;
            break;
        case Block::Workshop:
            c.iron = 60; c.components = 30; c.copper = 40;
            break;
        case Block::CO2Factory:
            c.iron = 60; c.coal = 50; c.copper = 30;
            break;
        case Block::Habitat:
            c.stone = 80; c.iron = 60; c.copper = 40; c.metal = 30;
            break;
        case Block::TerraformerBeacon:
            c.iron = 100; c.crystal = 50; c.components = 40; c.copper = 60;
            break;
        default: break;
    }
    return c;
}

std::string module_cost_string(const CraftCost& c) {
    std::string s;
    auto add = [&](const char* name, int need, int have) {
        if (need <= 0) return;
        if (!s.empty()) s += " ";
        s += name;
        s += ":" + std::to_string(need);
        if (have < need) s += "(!)";
    };
    add("Pedra", c.stone, g_inventory[(int)Block::Stone]);
    add("Ferro", c.iron, g_inventory[(int)Block::Iron]);
    add("Carvao", c.coal, g_inventory[(int)Block::Coal]);
    add("Madeira", c.wood, g_inventory[(int)Block::Wood]);
    add("Cobre", c.copper, g_inventory[(int)Block::Copper]);
    add("Gelo", c.ice, g_inventory[(int)Block::Ice]);
    add("Cristal", c.crystal, g_inventory[(int)Block::Crystal]);
    add("Metal", c.metal, g_inventory[(int)Block::Metal]);
    add("Organico", c.organic, g_inventory[(int)Block::Organic]);
    add("Comp", c.components, g_inventory[(int)Block::Components]);
    return s.empty() ? "Gratis" : s;
}

CraftCost module_cost(Block b) {
    CraftCost c{};
    switch (b) {
        case Block::SolarPanel:       c.iron = 3; c.stone = 2; break;
        case Block::WaterExtractor:   c.iron = 4; c.stone = 4; c.copper = 2; break;
        case Block::OxygenGenerator:  c.iron = 5; c.coal = 3; c.copper = 2; break;
        case Block::Greenhouse:       c.iron = 6; c.wood = 4; c.copper = 3; c.stone = 4; break;
        case Block::CO2Factory:       c.iron = 8; c.coal = 6; c.copper = 4; c.stone = 6; break;
        case Block::Habitat:          c.iron = 10; c.stone = 12; c.copper = 6; c.wood = 4; break;
        case Block::TerraformerBeacon: c.iron = 15; c.coal = 10; c.copper = 10; c.stone = 10; break;
        default: break;
    }
    return c;
}

// ============= can_afford/spend_cost/refund_cost dedup (Fase 1b of the plan) =============
// Originally these three functions each hand-listed the same 10 CraftCost fields against
// g_inventory[(int)Block::X] identically three times (30 near-identical lines total).
// Verified before deduplicating: CraftCost has exactly these 10 fields (stone, iron,
// coal, wood, copper, ice, crystal, metal, organic, components - see inventory_crafting.h)
// and all three original functions operated on all 10 in the same order with no field
// skipped or special-cased in one but not the others, so a single pointer-to-member table
// iterated by all three is behavior-preserving. cost_string() (below) is NOT part of this
// dedup - it only ever displayed 5 of the 10 fields by name (stone/iron/coal/copper/wood),
// which is pre-existing, unrelated behavior kept exactly as it was.
struct CraftCostField { Block block; int CraftCost::*field; };
static constexpr CraftCostField kCraftCostFields[] = {
    {Block::Stone, &CraftCost::stone}, {Block::Iron, &CraftCost::iron},
    {Block::Coal, &CraftCost::coal},   {Block::Wood, &CraftCost::wood},
    {Block::Copper, &CraftCost::copper}, {Block::Ice, &CraftCost::ice},
    {Block::Crystal, &CraftCost::crystal}, {Block::Metal, &CraftCost::metal},
    {Block::Organic, &CraftCost::organic}, {Block::Components, &CraftCost::components},
};

bool can_afford(const CraftCost& c) {
    for (auto& f : kCraftCostFields) {
        if (g_inventory[(int)f.block] < c.*f.field) return false;
    }
    return true;
}

void spend_cost(const CraftCost& c) {
    for (auto& f : kCraftCostFields) {
        g_inventory[(int)f.block] -= c.*f.field;
    }
}

void refund_cost(const CraftCost& c) {
    for (auto& f : kCraftCostFields) {
        g_inventory[(int)f.block] += c.*f.field;
    }
}

std::string cost_string(const CraftCost& c) {
    std::string s;
    auto add = [&](const char* name, int v) {
        if (v <= 0) return;
        if (!s.empty()) s += " ";
        s += name;
        s += std::to_string(v);
    };
    add("St", c.stone);
    add("Fe", c.iron);
    add("C", c.coal);
    add("Cu", c.copper);
    add("W", c.wood);
    return s.empty() ? "-" : s;
}
