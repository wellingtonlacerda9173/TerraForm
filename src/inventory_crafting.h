#pragma once

#include "blocks.h"

#include <array>
#include <string>

// ============= Inventory / Crafting =============
// Extracted verbatim from main.cpp (original lines ~255-258, 1729-1740, 1856-1890,
// 1898-1918, 2068-2136): the CraftCost struct, the cost-lookup/cost-string functions
// (get_module_cost/module_cost_string/cost_string - a second, cheaper `module_cost()`
// existed here until this session, when it was found to be a real cost-exploit and removed;
// get_module_cost() is now the only cost function), and the can_afford/spend_cost/
// refund_cost trio (deduplicated in an earlier stage - see inventory_crafting.cpp).
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

// Custo de construcao de um modulo - unica fonte de verdade, usada tanto pelo menu de
// construcao (modules_building.cpp) quanto pela colocacao instantanea via clique direito
// (building_interaction.cpp). Ate esta sessao existia um segundo `module_cost()` com custos
// ~10x mais baratos so pro caminho de clique direito - um exploit real, ja removido (o
// clique direito continua instantaneo/sem fila, mas agora cobra o mesmo preco).
CraftCost get_module_cost(Block b);
std::string module_cost_string(const CraftCost& c);
std::string cost_string(const CraftCost& c);

// Custo do upgrade de 1 nivel de um modulo ja construido (tecla R, ver
// try_upgrade_module() em modules_building.h) - mais barato que construir do zero, pesado
// em Metal/Components/Crystal (a MESMA carteira do reparo de traje abaixo - a tensao de
// escolha entre os dois e o ponto).
CraftCost get_module_upgrade_cost(Block b);

// Custo de um "topup" de reparo do traje (tecla F, ver g_suit_integrity em main.cpp) -
// mesma carteira do upgrade de modulo acima, de proposito.
CraftCost get_suit_repair_cost();

// Custo de 1 lote de refino (tecla G, numa Oficina construida - ver
// try_refine_at_workshop() em modules_building.h) - consome minerio bruto, produz
// Block::RefinedAlloy. Da ao Metal seu 2o uso de verdade (o 1o sendo upgrade/reparo).
CraftCost get_refine_cost();

// Custo de fabricar a Pistola de Laser (tecla P, numa Oficina construida - ver
// try_craft_laser_pistol() em modules_building.h). So se paga 1x - g_inventory[LaserPistol]
// vira 1 (posse), nao um contador que soma. Item de tecnologia avancada: pesado em
// Metal/Componentes/Cristal.
CraftCost get_weapon_cost();

bool can_afford(const CraftCost& c);
void spend_cost(const CraftCost& c);
void refund_cost(const CraftCost& c);
