#pragma once

#include "blocks.h"

#include <array>
#include <string>

// ============= Inventory / Crafting =============
// Extracted verbatim from main.cpp (original lines ~255-258, 1729-1740, 1856-1890,
// 1898-1918, 2068-2136): the CraftCost struct, the two cost-lookup/cost-string function
// pairs (get_module_cost/module_cost_string and module_cost/cost_string - see the
// comment above can_afford() in inventory_crafting.cpp for why there are two similarly
// named pairs; this is pre-existing duplication from before this refactor, not something
// this stage fixes), and the can_afford/spend_cost/refund_cost trio (the one function
// group actually deduplicated in this stage - see inventory_crafting.cpp).
//
// g_inventory/g_selected move here too: they are read/written far outside crafting logic
// (mining grants items, HUD/build-menu rendering reads them, save/load serializes them),
// but this is their most natural owner among the modules extracted so far - same
// reasoning that made world.cpp the owner of g_world.

struct CraftCost {
    int stone = 0;
    int iron = 0;
    int coal = 0;
    int wood = 0;
    int copper = 0;
    int ice = 0;
    int crystal = 0;
    int metal = 0;
    int organic = 0;
    int components = 0;
};

// O unico inventario/selecao do jogo. Definidos (nao-static) em inventory_crafting.cpp;
// main.cpp e player_physics.cpp continuam usando g_inventory/g_selected diretamente
// (mineracao, HUD, hotbar, save/load, starter kit) atraves desta declaracao extern -
// mesmo padrao de g_world/g_camera nos estagios anteriores.
extern std::array<int, kBlockTypeCount> g_inventory;
extern Block g_selected;

// Custo de construcao de um modulo (usado pelo sistema de fila de construcao/menu de
// build - modules_building.cpp).
CraftCost get_module_cost(Block b);
std::string module_cost_string(const CraftCost& c);

// Custo "instantaneo" de colocacao via clique direito (caminho alternativo e mais antigo
// de colocar um modulo - ver nota em main.cpp sobre a inconsistencia entre os dois
// caminhos de colocacao, fora do escopo desta etapa).
CraftCost module_cost(Block b);
std::string cost_string(const CraftCost& c);

bool can_afford(const CraftCost& c);
void spend_cost(const CraftCost& c);
void refund_cost(const CraftCost& c);
