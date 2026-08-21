#include "modules_building.h"

#include "inventory_crafting.h" // CraftCost, get_module_cost, can_afford, spend_cost
#include "world.h"              // World, g_world, object_block_at, surface_block_at, terraform_step, melt_ice_around, update_phase
#include "config_types.h"       // BaseConfig (type of the extern global below)
#include "game_state.h"         // Alert, UnlockProgress, rng_next_u32
#include "math_core.h"          // clamp01, compute_daylight
#include "noise.h"              // lerp
#include "player_physics.h"     // g_player
#include "objectives.h"         // notify_module_built, update_objectives
#include "items_particles.h"    // spawn_block_particles (efeito visual do upgrade)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

// Globais de estado de jogo ainda definidas em main.cpp (dono continua sendo main.cpp -
// nao fazem parte desta etapa de extracao). Todas ja eram nao-static em main.cpp
// (extraidas/expostas em fases anteriores); so precisamos da declaracao aqui tambem,
// mesmo padrao de g_physics_cfg em camera.cpp / g_terrain_cfg em world.cpp.
extern float g_base_energy;
extern float g_base_water;
extern float g_base_oxygen;
extern float g_base_food;
extern float g_base_integrity;
extern float g_player_oxygen;
extern float g_player_water;
extern float g_player_food;
extern float g_suit_integrity; // Ver comentario completo em main.cpp
extern float g_energy;      // Deprecated, use g_base_energy
extern float g_water_res;   // Deprecated, maps to g_player_water
extern float g_oxygen;      // Deprecated, maps to g_player_oxygen
extern float g_food;        // Deprecated, maps to g_player_food
extern float g_temperature;
extern float g_co2_level;
extern float g_atmosphere;
extern TerraPhase g_phase;
extern int g_base_x;
extern int g_base_y;
extern bool g_surface_dirty;
extern UnlockProgress g_unlocks;

// g_day_time/g_alerts/g_alert_cooldowns/g_base_cfg: same pattern, but each needed its
// "static" removed in main.cpp specifically because update_modules() below now reads/
// writes them from this other translation unit (they were previously only touched by
// code that stayed inside main.cpp).
extern float g_day_time;
extern std::vector<Alert> g_alerts;
extern std::unordered_map<std::string, float> g_alert_cooldowns;
extern BaseConfig g_base_cfg;

// add_alert()/update_shooting_stars(): both stay defined in main.cpp (alert system and
// sky/day-night system respectively - out of scope for this stage), but update_modules()
// below calls both, so each lost the "static" it had in main.cpp. Plain forward
// declarations here (pattern (a): the callee doesn't have an incomplete-type issue, so no
// wrapper function is needed like clear_construction_queue() used to be for
// ConstructionJob). The default arguments on add_alert() must be repeated here since this
// is the only declaration of it visible in this translation unit.
void add_alert(const std::string& msg, float r, float g, float b, float duration = 3.0f, float cooldown = 5.0f);
void update_shooting_stars(float dt, float day_phase);

// kBaseEnergyMax/kBaseWaterMax/kBaseOxygenMax/kBaseFoodMax/kBaseIntegrityMax/
// kBaseIntegrityDecayRate/kDayLength/kTempThawing are compile-time literals (not mutable
// state) defined in main.cpp, which keeps its own copies too (HUD rendering and other
// code that stays there also uses them). Since they're literals, not state, this file
// keeps its own static constexpr copies rather than sharing them via extern - same
// pattern as kTempThawing/kTempHabitable in world.cpp.
static constexpr float kBaseEnergyMax = 500.0f;
static constexpr float kBaseWaterMax = 200.0f;
static constexpr float kBaseOxygenMax = 200.0f;
static constexpr float kBaseFoodMax = 200.0f;
static constexpr float kBaseIntegrityMax = 100.0f;
static constexpr float kBaseIntegrityDecayRate = 0.5f;  // Per minute without workshop
// kDayLength agora vem de game_state.h (era uma copia local aqui).
static constexpr float kTempThawing = 0.0f;     // Water can be liquid

// ============= Construction / build slots / modules (state) =============
// Os unicos vetores de fila de construcao/slots/modulos do jogo. Definidos (nao-static)
// aqui: main.cpp continua usando g_construction_queue/g_build_slots/g_modules diretamente
// atraves da declaracao extern em modules_building.h.
std::vector<ConstructionJob> g_construction_queue;
std::vector<BuildSlotInfo> g_build_slots;
std::vector<Module> g_modules;

// ============= Generate Base (Landing Site) =============
void generate_base(World& world) {
    g_build_slots.clear();

    // Top-down: escolher um "bom ponto" perto do centro (evita agua/gelo e terreno muito inclinado)
    int center_x = world.w / 2;
    int center_y = world.h / 2;
    int best_x = center_x;
    int best_y = center_y;
    int best_score = std::numeric_limits<int>::min();

    // Margens: base/rocket/domo usam offsets negativos em Y (para "cima" no mapa)
    int margin_x = 40;
    int margin_y = 30;

    for (int y = center_y - 45; y <= center_y + 45; y += 2) {
        for (int x = center_x - 70; x <= center_x + 70; x += 2) {
            if (x < margin_x || x >= world.w - margin_x) continue;
            if (y < margin_y || y >= world.h - margin_y) continue;

            int score = 0;
            int16_t min_h = std::numeric_limits<int16_t>::max();
            int16_t max_h = std::numeric_limits<int16_t>::min();
            // Amostra uma "área de pouso" menor, suficiente para decidir.
            for (int dy = -10; dy <= 10; ++dy) {
                for (int dx = -18; dx <= 18; ++dx) {
                    int sx = x + dx;
                    int sy = y + dy;
                    if (!world.in_bounds(sx, sy)) { score -= 10; continue; }

                    int16_t hh = world.height_at(sx, sy);
                    min_h = std::min(min_h, hh);
                    max_h = std::max(max_h, hh);

                    // Penalizar objetos (rochas/minerios/modulos) na area de pouso
                    if (object_block_at(world, sx, sy) != Block::Air) score -= 6;

                    // Preferir solo seco/estavel
                    Block surface = surface_block_at(world, sx, sy);
                    if (surface == Block::Water || surface == Block::Ice) score -= 10;
                    else if (surface == Block::Snow) score -= 2;
                    else if (surface == Block::Sand) score += 1;
                    else if (surface == Block::Dirt) score += 2;
                    else if (surface == Block::Grass) score += 3;
                }
            }

            // Penalizar area inclinada (base precisa ser plana)
            int range = (int)max_h - (int)min_h;
            score -= range * 6;
            if (min_h <= 8) score -= 30; // muito perto de baixadas geladas

            if (score > best_score) {
                best_score = score;
                best_x = x;
                best_y = y;
            }
        }
    }

    g_base_x = best_x;
    int surface = best_y;
    g_base_y = surface;

    // === FLATTEN HEIGHTMAP (base precisa ser plana no terreno 3D) ===
    int16_t base_h = world.height_at(best_x, surface);
    for (int dy = -30; dy <= 25; ++dy) {
        for (int dx = -40; dx <= 40; ++dx) {
            int tx = best_x + dx;
            int ty = surface + dy;
            if (!world.in_bounds(tx, ty)) continue;
            world.set_height(tx, ty, base_h);
            // Limpar objetos existentes (rochas/minerios) para nao poluir a base
            if (object_block_at(world, tx, ty) != Block::Air) {
                world.set(tx, ty, Block::Air);
            }
        }
    }

    // === PLATAFORMA CIRCULAR DA BASE (3D) ===
    // A base agora e centrada no domo geodesico decorativo (malha sem colisao, desenhada em
    // main.cpp - ver render_geodesic_dome). So a plataforma solida por baixo precisa de
    // blocos; o domo em si nao bloqueia passagem, entao nao precisa de vao fisico de porta
    // aqui - o jogador ja anda livremente por baixo dele em qualquer direcao.
    static constexpr float kPadRadius = 20.0f;

    // Levantar 1 unidade de heightmap (=> 0.25 no mundo) para dar volume na borda.
    int16_t pad_h = (int16_t)std::clamp((int)base_h + 1, 0, 256);

    {
        float pad_r2 = kPadRadius * kPadRadius;
        int pad_span = (int)kPadRadius + 1;
        for (int dy = -pad_span; dy <= pad_span; ++dy) {
            for (int dx = -pad_span; dx <= pad_span; ++dx) {
                if ((float)(dx * dx + dy * dy) > pad_r2) continue;
                int tx = best_x + dx;
                int ty = surface + dy;
                if (!world.in_bounds(tx, ty)) continue;

                world.set_height(tx, ty, pad_h);
                // Limpar objetos existentes (rochas/minerios) para nao poluir a base
                if (object_block_at(world, tx, ty) != Block::Air) {
                    world.set(tx, ty, Block::Air);
                }
                world.set_ground(tx, ty, Block::LandingPad);
                world.set(tx, ty, Block::LandingPad);
            }
        }
    }

    auto place_slot = [&](int sx, int sy, const std::string& label) {
        if (!world.in_bounds(sx, sy)) return;
        world.set_ground(sx, sy, Block::BuildSlot);
        world.set(sx, sy, Block::BuildSlot);
        g_build_slots.push_back({sx, sy, Block::Air, label});
    };

    // === SLOTS DE CONSTRUCAO (em anel ao redor do domo, nao mais numa grade espalhada) ===
    int cx = best_x;
    int cy = surface;

    // NOTA: a parede da cupula NAO e mais feita de blocos do mundo (2 tentativas anteriores -
    // sem altura e depois com altura - ficaram com contorno serrilhado/pontudo, uma grade
    // quadrada tentando aproximar um circulo, e o usuario rejeitou o visual de novo mesmo
    // com a malha lisa por cima). A colisao de verdade agora e uma barreira cilindrica
    // invisivel calculada matematicamente (kDomeWallRadius, modules_building.h; aplicada em
    // player_physics.cpp/apply_dome_barrier) - um circulo perfeito, sem serrilhado nenhum. A
    // porta "sempre fechada" e so um detalhe de cor/moldura na propria malha decorativa
    // (render_geodesic_dome em main.cpp, secao da saia/fundacao) - nao existe bloco de porta
    // nenhum no mundo.

    static const char* kSlotLabels[10] = {
        "Solar 1", "Solar 2", "Solar 3", "Water Extractor", "O2 Generator",
        "Greenhouse 1", "Greenhouse 2", "CO2 Factory", "Terraformer", "Habitat"
    };
    const float kSlotRingRadius = 14.0f;
    // SEM deslocamento de fase de proposito: com 10 slots a 36 graus um do outro, um
    // deslocamento de meia posicao (pi/10 = 18 graus) na verdade ALINHA um slot exatamente
    // no eixo norte/sul (18+36*2=90, 18+36*7=270) - o oposto do que o comentario original
    // aqui dizia querer. Sem deslocamento nenhum, 36*i nunca bate em 90/270 pra i inteiro
    // (90/36 nao e inteiro), entao nenhum slot cai na linha da porta.
    for (int i = 0; i < 10; ++i) {
        float angle = (float)i / 10.0f * 2.0f * kPi;
        int sx = cx + (int)std::lround(std::cos(angle) * kSlotRingRadius);
        int sy = cy + (int)std::lround(std::sin(angle) * kSlotRingRadius);
        place_slot(sx, sy, kSlotLabels[i]);
    }

    // === DECORACAO 3D (simples) - fora do anel de slots ===
    // "Wreck" de foguete, reposicionado (radio ~25) pra nao colidir com o anel de slots
    // (raio 14) nem com a parede/porta (lado sul, dy>0).
    {
        int rx = cx + 18;
        int ry = cy - 18;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (world.in_bounds(rx + dx, ry + dy)) world.set(rx + dx, ry + dy, Block::RocketHull);
            }
        }
        if (world.in_bounds(rx, ry)) world.set(rx, ry, Block::RocketEngine);
        if (world.in_bounds(rx, ry - 2)) world.set(rx, ry - 2, Block::RocketNose);
    }

    // NOTA: o "abrigo interior" estilo GTA (quartinho mobiliado bem distante, acessado por
    // teleporte) foi removido a pedido do usuario - com a cupula agora tendo colisao de
    // verdade, todo passeio pra fora da base (minerar/explorar) teria que passar pela porta,
    // o que forcaria um teleporte indesejado pro quartinho distante toda vez. A porta agora
    // so cruza a propria parede da cupula (ver bloco acima) - um pulo curto, nao uma viagem
    // longa - entao sair da base pra explorar funciona normalmente.

    // O slot de construcao inicial fica vazio de proposito (nao ha mais um painel solar
    // pre-construido aqui): o primeiro objetivo do jogador ("Gerar energia") e justamente
    // construir seu primeiro modulo de energia com a propria mao, no slot que ja existe
    // perto da base - um tutorial natural para a mecanica de construcao, em vez de o
    // jogador so descobrir a mecanica minerando/recolocando um modulo que ja existia.

    world.rebuild_surface_cache();
}

void rebuild_modules_from_world() {
    g_modules.clear();
    if (!g_world) return;
    for (int y = 0; y < g_world->h; ++y) {
        for (int x = 0; x < g_world->w; ++x) {
            Block b = g_world->get(x, y);
            if (is_module(b)) g_modules.push_back(Module{x, y, b, 0.0f});
        }
    }
}

// ============================================================================
// MODULE & RESOURCE SYSTEM - Complete Gameplay Loop
// ============================================================================

// Get module statistics
ModuleStats get_module_stats(Block b) {
    ModuleStats s{};
    switch (b) {
        case Block::SolarPanel:
            s.name = "Painel Solar";
            s.description = "Gera energia basica";
            s.energy_production = 3.0f;
            s.construction_time = 15.0f;
            break;
        case Block::EnergyGenerator:
            s.name = "Gerador de Energia";
            s.description = "Fonte principal de energia";
            s.energy_production = 8.0f;
            s.energy_consumption = 0.0f;
            s.construction_time = 45.0f;
            break;
        case Block::OxygenGenerator:
            s.name = "Gerador de Oxigenio";
            s.description = "Produz O2 para a base";
            s.oxygen_production = 2.0f;
            s.energy_consumption = 1.0f;
            s.construction_time = 30.0f;
            break;
        case Block::WaterExtractor:
            s.name = "Purificador de Agua";
            s.description = "Extrai e purifica agua";
            s.water_production = 1.5f;
            s.energy_consumption = 0.8f;
            s.construction_time = 25.0f;
            break;
        case Block::Greenhouse:
            s.name = "Estufa";
            s.description = "Produz comida";
            s.food_production = 1.0f;
            s.energy_consumption = 0.5f;
            s.construction_time = 40.0f;
            break;
        case Block::Workshop:
            s.name = "Oficina";
            s.description = "Repara a base";
            s.integrity_bonus = 2.0f;
            s.energy_consumption = 1.5f;
            s.construction_time = 60.0f;
            break;
        case Block::CO2Factory:
            s.name = "Fabrica de CO2";
            s.description = "Aquece o planeta";
            s.co2_production = 0.5f;
            s.energy_consumption = 2.0f;
            s.construction_time = 50.0f;
            break;
        case Block::Habitat:
            s.name = "Habitat";
            s.description = "Moradia extra";
            s.energy_consumption = 0.3f;
            s.construction_time = 90.0f;
            break;
        case Block::TerraformerBeacon:
            s.name = "Terraformador";
            s.description = "Terraformacao avancada";
            s.energy_consumption = 5.0f;
            s.construction_time = 120.0f;
            break;
        default:
            s.name = "Unknown";
            s.description = "";
            break;
    }
    return s;
}

// Get module status for display. NOTE: unused anywhere in the codebase (pre-existing
// dead code from before this refactor) - stays static since nothing outside this file
// calls it.
static ModuleStatus get_module_status(Block b) {
    // Check if under construction
    for (const auto& job : g_construction_queue) {
        if (job.active && job.module_type == b) {
            return ModuleStatus::Building;
        }
    }

    // Check if we have resources
    CraftCost cost = get_module_cost(b);
    if (!can_afford(cost)) {
        return ModuleStatus::Blocked;
    }

    return ModuleStatus::Available;
}

// NOTE: also unused anywhere in the codebase (pre-existing dead code) - stays static.
static const char* status_string(ModuleStatus s) {
    switch (s) {
        case ModuleStatus::Available: return "DISPONIVEL";
        case ModuleStatus::Blocked: return "BLOQUEADO";
        case ModuleStatus::Building: return "CONSTRUINDO";
        case ModuleStatus::Active: return "ATIVO";
        case ModuleStatus::NoPower: return "SEM ENERGIA";
        case ModuleStatus::Damaged: return "DANIFICADO";
        default: return "???";
    }
}

// Start construction of a module
bool start_construction(Block module_type, int slot_index) {
    CraftCost cost = get_module_cost(module_type);
    if (!can_afford(cost)) {
        add_alert("Recursos insuficientes!", 1.0f, 0.3f, 0.3f);
        return false;
    }

    spend_cost(cost);

    ModuleStats stats = get_module_stats(module_type);
    ConstructionJob job;
    job.module_type = module_type;
    job.slot_index = slot_index;
    job.time_remaining = stats.construction_time;
    job.total_time = stats.construction_time;
    job.active = true;
    g_construction_queue.push_back(job);

    add_alert("Construcao iniciada: " + std::string(stats.name), 0.3f, 1.0f, 0.5f);
    return true;
}

// Upgrade de 1 nivel (tecla R, main.cpp) - ver comentario completo em modules_building.h.
bool try_upgrade_module(int tx, int ty) {
    for (Module& m : g_modules) {
        if (m.x != tx || m.y != ty) continue;
        if (m.upgraded) return false;

        CraftCost cost = get_module_upgrade_cost(m.type);
        if (!can_afford(cost)) {
            add_alert("Recursos insuficientes pro upgrade!", 1.0f, 0.3f, 0.3f);
            return false;
        }

        spend_cost(cost);
        m.upgraded = true;
        ModuleStats stats = get_module_stats(m.type);
        add_alert("Modulo aprimorado: " + std::string(stats.name), 0.3f, 1.0f, 0.5f);
        if (g_world) {
            spawn_block_particles(Block::Crystal, tile_center(tx), tile_center(ty), g_world->h);
        }
        return true;
    }
    return false;
}

// Refino de Liga (tecla G, main.cpp) - ver comentario completo em modules_building.h.
// Cooldown fixo em vez de fila tipo start_construction(): uma fila exigiria
// struct/save/UI de progresso novos so pra um recurso cujo unico consumidor hoje e o
// objetivo de legado "Refinar um legado" (objectives.cpp) - cooldown simples basta.
static constexpr int kRefineBatchSize = 5;
static constexpr float kRefineCooldownSeconds = 4.0f;
static float g_refine_cooldown = 0.0f;

bool try_refine_at_workshop(int tx, int ty) {
    if (g_refine_cooldown > 0.0f) {
        set_toast("Oficina recarregando...", 1.0f);
        return false;
    }
    for (Module& m : g_modules) {
        if (m.x != tx || m.y != ty || m.type != Block::Workshop) continue;
        if (m.status == ModuleStatus::Damaged) {
            add_alert("Oficina danificada - repare antes de refinar!", 1.0f, 0.4f, 0.2f);
            return false;
        }

        CraftCost cost = get_refine_cost();
        if (!can_afford(cost)) {
            add_alert("Recursos insuficientes para refinar!", 1.0f, 0.3f, 0.3f);
            return false;
        }

        spend_cost(cost);
        g_inventory[(int)Block::RefinedAlloy] += kRefineBatchSize;
        g_refine_cooldown = kRefineCooldownSeconds;
        add_alert("Liga Refinada produzida! (+" + std::to_string(kRefineBatchSize) + ")", 0.3f, 1.0f, 0.5f);
        if (g_world) {
            spawn_block_particles(Block::Metal, tile_center(tx), tile_center(ty), g_world->h);
        }
        return true;
    }
    return false;
}

// Fabricacao "de campo" da Pistola de Laser (tecla P, main.cpp, funciona em qualquer lugar -
// pedido do jogador: a versao original exigia mirar uma Oficina construida, mas isso
// virava um tech avancado de meio-jogo pra uma ameaca que devia ser leve/opcional desde
// cedo). E uma posse (0 ou 1), nao um lote que soma - fabricar de novo com uma ja em maos
// e bloqueado antes de gastar recurso a toa.
bool try_craft_laser_pistol() {
    if (g_inventory[(int)Block::LaserPistol] > 0) {
        set_toast("Voce ja possui uma Pistola de Laser.", 1.5f);
        return false;
    }

    CraftCost cost = get_weapon_cost();
    if (!can_afford(cost)) {
        add_alert("Recursos insuficientes para fabricar a pistola! (" + module_cost_string(cost) + ")", 1.0f, 0.3f, 0.3f);
        return false;
    }

    spend_cost(cost);
    g_inventory[(int)Block::LaserPistol] = 1;
    add_alert("Pistola de Laser fabricada!", 0.3f, 1.0f, 0.5f);
    if (g_world) {
        spawn_block_particles(Block::Metal, g_player.pos.x, g_player.pos.y, g_world->h);
    }
    return true;
}

// Legacy unlock requirements (for backward compatibility)
struct UnlockRequirement {
    int stone = 0;
    int iron = 0;
    int coal = 0;
    int copper = 0;
    int wood = 0;
    int ice = 0;
    int crystal = 0;
    int metal = 0;
    int organic = 0;
    int components = 0;
};

// Rebalanceamento: os thresholds antigos eram 4-10x mais baratos que o custo de
// construcao real (get_module_cost(), inventory_crafting.cpp) e nunca checavam gelo/
// cristal/metal/organico/componentes mesmo quando o modulo realmente precisa deles -
// unlock virava so decorativo (sempre desbloqueado bem antes de dar pra construir).
// Novos thresholds ~60% do custo real, usando os recursos que o modulo de fato pede.
static UnlockRequirement get_unlock_requirement(Block b) {
    UnlockRequirement r{};
    switch (b) {
        case Block::SolarPanel:       break;  // Ja desbloqueado (primeiro modulo do jogo)
        case Block::WaterExtractor:   r.ice = 18; r.metal = 12; r.copper = 9; break;
        case Block::OxygenGenerator:  r.ice = 30; r.iron = 30; r.copper = 12; break;
        case Block::Greenhouse:       r.organic = 24; r.iron = 15; r.ice = 15; break;
        case Block::CO2Factory:       r.iron = 36; r.coal = 30; r.copper = 18; break;
        case Block::Habitat:          r.stone = 48; r.iron = 36; r.copper = 24; r.metal = 18; break;
        case Block::TerraformerBeacon: r.iron = 60; r.crystal = 30; r.components = 24; r.copper = 36; break;
        default: break;
    }
    return r;
}

bool is_unlocked(Block b) {
    switch (b) {
        case Block::SolarPanel:       return g_unlocks.solar_unlocked;
        case Block::WaterExtractor:   return g_unlocks.water_extractor_unlocked;
        case Block::OxygenGenerator:  return g_unlocks.o2_generator_unlocked;
        case Block::Greenhouse:       return g_unlocks.greenhouse_unlocked;
        case Block::CO2Factory:       return g_unlocks.co2_factory_unlocked;
        case Block::Habitat:          return g_unlocks.habitat_unlocked;
        case Block::TerraformerBeacon: return g_unlocks.terraformer_unlocked;
        default: return true; // Non-modules are always available
    }
}

void check_unlocks() {
    // Check and unlock modules based on total collected resources
    auto check = [](bool& flag, const UnlockRequirement& r) {
        if (flag) return;
        if (g_unlocks.total_stone >= r.stone &&
            g_unlocks.total_iron >= r.iron &&
            g_unlocks.total_coal >= r.coal &&
            g_unlocks.total_copper >= r.copper &&
            g_unlocks.total_wood >= r.wood &&
            g_unlocks.total_ice >= r.ice &&
            g_unlocks.total_crystal >= r.crystal &&
            g_unlocks.total_metal >= r.metal &&
            g_unlocks.total_organic >= r.organic &&
            g_unlocks.total_components >= r.components) {
            flag = true;
        }
    };

    check(g_unlocks.solar_unlocked, get_unlock_requirement(Block::SolarPanel));
    check(g_unlocks.water_extractor_unlocked, get_unlock_requirement(Block::WaterExtractor));
    check(g_unlocks.o2_generator_unlocked, get_unlock_requirement(Block::OxygenGenerator));
    check(g_unlocks.greenhouse_unlocked, get_unlock_requirement(Block::Greenhouse));
    check(g_unlocks.co2_factory_unlocked, get_unlock_requirement(Block::CO2Factory));
    check(g_unlocks.habitat_unlocked, get_unlock_requirement(Block::Habitat));

    // Terraformer only unlocks after all survival modules are built
    if (!g_unlocks.terraformer_unlocked) {
        bool has_survival = false;
        for (const auto& m : g_modules) {
            if (m.type == Block::Habitat) has_survival = true;
        }
        // Need habitat + basic modules unlocked
        if (has_survival && g_unlocks.habitat_unlocked &&
            g_unlocks.o2_generator_unlocked && g_unlocks.greenhouse_unlocked) {
            UnlockRequirement r = get_unlock_requirement(Block::TerraformerBeacon);
            if (g_unlocks.total_stone >= r.stone &&
                g_unlocks.total_iron >= r.iron &&
                g_unlocks.total_coal >= r.coal &&
                g_unlocks.total_copper >= r.copper &&
                g_unlocks.total_crystal >= r.crystal &&
                g_unlocks.total_components >= r.components) {
                g_unlocks.terraformer_unlocked = true;
            }
        }
    }
}

std::string unlock_progress_string(Block b) {
    UnlockRequirement r = get_unlock_requirement(b);
    std::string s;
    auto add = [&](const char* name, int have, int need) {
        if (need <= 0) return;
        if (!s.empty()) s += " ";
        s += name;
        s += std::to_string(have) + "/" + std::to_string(need);
    };
    add("St", g_unlocks.total_stone, r.stone);
    add("Fe", g_unlocks.total_iron, r.iron);
    add("C", g_unlocks.total_coal, r.coal);
    add("Cu", g_unlocks.total_copper, r.copper);
    add("W", g_unlocks.total_wood, r.wood);
    add("Gl", g_unlocks.total_ice, r.ice);
    add("Cr", g_unlocks.total_crystal, r.crystal);
    add("Mt", g_unlocks.total_metal, r.metal);
    add("Or", g_unlocks.total_organic, r.organic);
    add("Cp", g_unlocks.total_components, r.components);
    return s;
}

void update_modules(World& world, float dt) {
    g_day_time += dt;

    float day_phase = std::fmod(g_day_time, kDayLength) / kDayLength;
    float daylight = compute_daylight(day_phase);

    update_shooting_stars(dt, day_phase);

    // Update alerts timer
    for (auto it = g_alerts.begin(); it != g_alerts.end();) {
        it->time_remaining -= dt;
        if (it->time_remaining <= 0.0f) {
            it = g_alerts.erase(it);
        } else {
            ++it;
        }
    }

    // Update alert cooldowns
    for (auto& pair : g_alert_cooldowns) {
        if (pair.second > 0.0f) {
            pair.second -= dt;
        }
    }

    // ========== PROCESS CONSTRUCTION QUEUE ==========
    for (auto& job : g_construction_queue) {
        if (!job.active) continue;

        // Construction requires energy
        float energy_cost = 2.0f * dt;
        if (g_base_energy >= energy_cost) {
            g_base_energy -= energy_cost;
            job.time_remaining -= dt;

            if (job.time_remaining <= 0.0f) {
                // Construction complete!
                job.active = false;

                // Place the module
                if (job.slot_index >= 0 && job.slot_index < (int)g_build_slots.size()) {
                    BuildSlotInfo& slot = g_build_slots[job.slot_index];
                    slot.assigned_module = job.module_type;
                    world.set(slot.x, slot.y, job.module_type);

                    Module mod;
                    mod.type = job.module_type;
                    mod.x = slot.x;
                    mod.y = slot.y;
                    mod.t = 0.0f;
                    g_modules.push_back(mod);
                    notify_module_built(job.module_type);
                }

                ModuleStats stats = get_module_stats(job.module_type);
                add_alert("Construido: " + std::string(stats.name), 0.3f, 1.0f, 0.5f, 4.0f);
            }
        } else {
            add_alert("Construcao parada - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // Clean up completed jobs
    g_construction_queue.erase(
        std::remove_if(g_construction_queue.begin(), g_construction_queue.end(),
            [](const ConstructionJob& j) { return !j.active; }),
        g_construction_queue.end());

    // ========== UPDATE MODULE STATUS ==========
    // Check energy and health for each module
    for (Module& m : g_modules) {
        // Degrade health slowly over time (0.5% per minute)
        float health_decay = 0.5f / 60.0f * dt;
        m.health = std::max(0.0f, m.health - health_decay);

        // Determine status
        if (m.health <= 0.0f) {
            m.status = ModuleStatus::Damaged;
        } else if (g_base_energy <= 0.0f && m.type != Block::SolarPanel && m.type != Block::EnergyGenerator) {
            m.status = ModuleStatus::NoPower;
        } else {
            m.status = ModuleStatus::Active;
        }
    }

    // Count ACTIVE modules (damaged modules don't produce) - peso em vez de contagem
    // simples: um modulo aprimorado (Module::upgraded, tecla R) conta kModuleUpgradeMult
    // em vez de 1, entao toda formula abaixo (ja escrita como (float)xxx_count * taxa)
    // ganha o boost automaticamente, sem reescrever nenhuma delas.
    static constexpr float kModuleUpgradeMult = 1.5f;
    float solar_count = 0.0f;
    float energy_gen_count = 0.0f;
    float water_count = 0.0f;
    float o2_count = 0.0f;
    float greenhouse_count = 0.0f;
    float workshop_count = 0.0f;
    float co2_factory_count = 0.0f;
    float habitat_count = 0.0f;
    int beacon_count = 0;

    for (const Module& m : g_modules) {
        // Skip damaged modules
        if (m.status == ModuleStatus::Damaged) continue;

        float w = m.upgraded ? kModuleUpgradeMult : 1.0f;
        switch (m.type) {
            case Block::SolarPanel: solar_count += w; break;
            case Block::EnergyGenerator: energy_gen_count += w; break;
            case Block::WaterExtractor: water_count += w; break;
            case Block::OxygenGenerator: o2_count += w; break;
            case Block::Greenhouse: greenhouse_count += w; break;
            case Block::Workshop: workshop_count += w; break;
            case Block::CO2Factory: co2_factory_count += w; break;
            case Block::Habitat: habitat_count += w; break;
            case Block::TerraformerBeacon: beacon_count++; break;
            default: break;
        }
    }

    // ========== BASE CONSTANT CONSUMPTION ==========
    // The base always consumes resources (per minute converted to per second)
    float base_o2_consumption = 1.0f / 60.0f * dt;    // -1 O2/min
    float base_energy_consumption = 2.0f / 60.0f * dt; // -2 Energy/min
    float base_water_consumption = 1.0f / 60.0f * dt;  // -1 Water/min

    g_base_oxygen = std::max(0.0f, g_base_oxygen - base_o2_consumption);
    g_base_energy = std::max(0.0f, g_base_energy - base_energy_consumption);
    g_base_water = std::max(0.0f, g_base_water - base_water_consumption);

    // ========== BASE INTEGRITY DECAY ==========
    // Without workshop, integrity slowly decays
    float integrity_decay = (kBaseIntegrityDecayRate / 60.0f) * dt;
    if (workshop_count == 0) {
        g_base_integrity = std::max(0.0f, g_base_integrity - integrity_decay);
    }

    // ========== SOLAR PANELS ==========
    // Generate energy for the BASE (rate per minute: +3/panel)
    float solar_efficiency = 0.7f + 0.3f * clamp01(g_atmosphere / 50.0f);
    float solar_rate = 3.0f / 60.0f;  // Per second
    float energy_produced = (float)solar_count * solar_rate * daylight * solar_efficiency * dt;
    g_base_energy = std::clamp(g_base_energy + energy_produced, 0.0f, kBaseEnergyMax);

    // ========== ENERGY GENERATORS ==========
    // Main power source (+8 energy/min)
    if (energy_gen_count > 0) {
        float gen_rate = 8.0f / 60.0f;  // Per second
        float gen_produced = (float)energy_gen_count * gen_rate * dt;
        g_base_energy = std::clamp(g_base_energy + gen_produced, 0.0f, kBaseEnergyMax);
    }

    // ========== WATER EXTRACTORS ==========
    // Extract water (+1.5/min, costs -0.8 energy/min)
    if (water_count > 0) {
        float e_cost = (0.8f / 60.0f) * (float)water_count * dt;
        float water_rate = 1.5f / 60.0f;  // Per second

        if (g_base_energy >= e_cost) {
            g_base_energy -= e_cost;
            float temp_bonus = clamp01((g_temperature + 60.0f) / 80.0f);
            float water_produced = (float)water_count * water_rate * (0.5f + 0.5f * temp_bonus) * dt;
            g_base_water = std::clamp(g_base_water + water_produced, 0.0f, kBaseWaterMax);
        } else {
            add_alert("Purificador parado - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // ========== OXYGEN GENERATORS ==========
    // Produce O2 (+2/min, costs -1 energy/min)
    if (o2_count > 0) {
        float e_cost = (1.0f / 60.0f) * (float)o2_count * dt;
        float o2_rate = 2.0f / 60.0f;  // Per second

        if (g_base_energy >= e_cost) {
            g_base_energy -= e_cost;
            float o2_produced = (float)o2_count * o2_rate * dt;
            g_base_oxygen = std::clamp(g_base_oxygen + o2_produced, 0.0f, kBaseOxygenMax);
            g_atmosphere = std::clamp(g_atmosphere + o2_produced * 0.1f, 0.0f, 100.0f);
        } else {
            add_alert("Gerador O2 parado - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // ========== GREENHOUSES ==========
    // Produce food (+1/min, costs -0.5 energy/min, needs water)
    if (greenhouse_count > 0) {
        float e_cost = (0.5f / 60.0f) * (float)greenhouse_count * dt;
        float w_cost = (0.3f / 60.0f) * (float)greenhouse_count * dt;
        float food_rate = 1.0f / 60.0f;  // Per second

        if (g_base_water <= 0.0f) {
            add_alert("Estufa parada - Sem agua!", 0.2f, 0.6f, 1.0f);
        } else if (g_base_energy >= e_cost && g_base_water >= w_cost) {
            g_base_energy -= e_cost;
            g_base_water -= w_cost;
            float food_produced = (float)greenhouse_count * food_rate * dt;
            g_base_food = std::clamp(g_base_food + food_produced, 0.0f, kBaseFoodMax);
            g_base_oxygen = std::clamp(g_base_oxygen + food_produced * 0.2f, 0.0f, kBaseOxygenMax);
        } else {
            add_alert("Estufa parada - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // ========== WORKSHOP ==========
    // Repairs base integrity (+2/min, costs -1.5 energy/min)
    if (workshop_count > 0) {
        float e_cost = (1.5f / 60.0f) * (float)workshop_count * dt;
        float repair_rate = 2.0f / 60.0f;  // Per second
        float module_repair_rate = 5.0f / 60.0f;  // 5% health per minute per workshop

        if (g_base_energy >= e_cost) {
            g_base_energy -= e_cost;

            // Repair base integrity
            float repair = (float)workshop_count * repair_rate * dt;
            g_base_integrity = std::clamp(g_base_integrity + repair, 0.0f, kBaseIntegrityMax);

            // Repair damaged modules
            for (Module& m : g_modules) {
                if (m.health < 100.0f) {
                    m.health = std::min(100.0f, m.health + module_repair_rate * (float)workshop_count * dt);
                }
            }
        } else {
            add_alert("Oficina parada - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // Cooldown do refino (tecla G, try_refine_at_workshop) - independente de ter Workshop
    // ou nao (so relevante quando o jogador aciona a acao, mas decrementa sempre por
    // simplicidade, mesmo padrao de outros timers deste arquivo).
    if (g_refine_cooldown > 0.0f) g_refine_cooldown = std::max(0.0f, g_refine_cooldown - dt);

    // ========== CO2 FACTORIES ==========
    // Release CO2 to warm the planet (costs -2 energy/min)
    if (co2_factory_count > 0) {
        float e_cost = (2.0f / 60.0f) * (float)co2_factory_count * dt;

        if (g_base_energy >= e_cost) {
            g_base_energy -= e_cost;

            float co2_rate = 0.5f / 60.0f;  // Per second
            float co2_produce = (float)co2_factory_count * co2_rate * dt;
            g_co2_level = std::clamp(g_co2_level + co2_produce, 0.0f, 100.0f);

            float warming_rate = 0.2f * (float)co2_factory_count * (1.0f - g_temperature / 50.0f);
            g_temperature = std::clamp(g_temperature + warming_rate * dt / 60.0f, -60.0f, 40.0f);

            g_atmosphere = std::clamp(g_atmosphere + co2_produce * 0.5f, 0.0f, 100.0f);
        } else {
            add_alert("Fabrica CO2 parada - Sem energia!", 1.0f, 0.5f, 0.2f);
        }
    }

    // ========== HABITATS ==========
    // Provide shelter (minimal consumption -0.3 energy/min)
    if (habitat_count > 0) {
        float e_cost = (0.3f / 60.0f) * (float)habitat_count * dt;
        if (g_base_energy >= e_cost) {
            g_base_energy -= e_cost;
            // Small passive O2 recycling
            g_base_oxygen = std::clamp(g_base_oxygen + 0.3f * (float)habitat_count * dt / 60.0f, 0.0f, kBaseOxygenMax);
        }
    }

    // ========== TERRAFORMER BEACONS ==========
    // Advanced terraforming (costs -5 energy/min)
    if (g_phase >= TerraPhase::Thawing) {
        for (Module& m : g_modules) {
            if (m.type != Block::TerraformerBeacon) continue;

            float e_cost = (5.0f / 60.0f) * dt;
            if (g_base_energy >= e_cost && g_base_water >= 1.0f) {
                g_base_energy -= e_cost;

                // Aprimorado: limiar menor = tique mais frequente pro mesmo tempo
                // acumulado (~1.5x mais rapido) - mesmo boost conceitual do
                // kModuleUpgradeMult usado nos outros modulos, so que expresso como
                // limiar em vez de contador (este modulo ja e tratado por instancia).
                float tick_thr = m.upgraded ? 0.10f : 0.15f;
                m.t += dt;
                while (m.t >= tick_thr && g_base_water > 0.5f) {
                    m.t -= tick_thr;
                    g_base_water = std::max(0.0f, g_base_water - 0.5f);
                    terraform_step(world, m.x, m.y);
                    melt_ice_around(world, m.x, m.y, 8);
                }
            } else {
                add_alert("Terraformador parado - Recursos!", 0.8f, 0.3f, 0.8f);
            }
        }
    }

    // ========== PLAYER IS AT BASE - ZONA SEGURA ==========
    float dx_base = g_player.pos.x - (float)g_base_x;
    float dy_base = g_player.pos.y - (float)g_base_y;
    float dist_to_base = std::sqrt(dx_base * dx_base + dy_base * dy_base);
    bool at_base = (dist_to_base < g_base_cfg.safe_radius);  // Usar raio configuravel

    if (at_base) {
        // Recharge O2 from base storage (consumes base O2!)
        if (g_player_oxygen < 100.0f && g_base_oxygen > 0.0f) {
            float need = std::min(g_base_cfg.recharge_oxygen_rate * dt, 100.0f - g_player_oxygen);
            float o2_cost = need * 0.20f;  // Costs 20% extra O2 from base
            float available = std::min(need, g_base_oxygen - o2_cost);
            if (available > 0.0f) {
                g_player_oxygen += available;
                g_base_oxygen -= (available + o2_cost);
            }
        }

        // Recharge water from base storage
        if (g_player_water < 100.0f && g_base_water > 0.0f) {
            float need = std::min(g_base_cfg.recharge_water_rate * dt, 100.0f - g_player_water);
            float available = std::min(need, g_base_water);
            g_player_water += available;
            g_base_water -= available;
        }

        // Recharge food from base storage (slowest)
        if (g_player_food < 100.0f && g_base_food > 0.0f) {
            float need = std::min(g_base_cfg.recharge_food_rate * dt, 100.0f - g_player_food);
            float available = std::min(need, g_base_food);
            g_player_food += available;
            g_base_food -= available;
        }

        // Reparar HP do jogador na zona segura (gratis)
        if (g_player.hp < 100) {
            g_player.hp = std::min(100, g_player.hp + (int)(g_base_cfg.repair_player_hp_per_sec * dt + 0.5f));
        }

        // Reabastecer jetpack na zona segura (usa energia da base)
        if (g_player.jetpack_fuel < 100.0f && g_base_energy > 5.0f) {
            float fuel_need = std::min(g_base_cfg.jetpack_refuel_per_sec * dt, 100.0f - g_player.jetpack_fuel);
            float energy_cost = fuel_need * 0.1f;  // Consome energia da base
            if (g_base_energy >= energy_cost) {
                g_player.jetpack_fuel += fuel_need;
                g_base_energy -= energy_cost;
            }
        }

        // Can't recharge if base O2 too low!
        if (g_base_oxygen < 10.0f && g_player_oxygen < 50.0f) {
            add_alert("Oxigenio da base muito baixo!", 1.0f, 0.3f, 0.3f);
        }
    }

    // ========== FAILURE CONSEQUENCES ==========

    // Oxygen = 0 -> Can't recharge player
    if (g_base_oxygen <= 0.0f) {
        add_alert("O2 ZERADO - Nao pode recarregar!", 1.0f, 0.2f, 0.2f);
    } else if (g_base_oxygen < 20.0f) {
        add_alert("O2 BAIXO", 1.0f, 0.6f, 0.2f);
    }

    // Energy = 0 -> Modules shut down
    if (g_base_energy <= 0.0f) {
        add_alert("ENERGIA CRITICA - Modulos desligados!", 1.0f, 0.8f, 0.2f);
    } else if (g_base_energy < 20.0f) {
        add_alert("Energia baixa", 1.0f, 0.8f, 0.4f);
    }

    // Check for damaged modules
    int damaged_count = 0;
    for (const Module& m : g_modules) {
        if (m.status == ModuleStatus::Damaged) damaged_count++;
    }
    if (damaged_count > 0) {
        add_alert("Modulos danificados: " + std::to_string(damaged_count) + " - Construa Oficina!", 1.0f, 0.5f, 0.2f);
    }

    // Integrity = 0 -> Base collapse (severe damage)
    if (g_base_integrity <= 0.0f) {
        add_alert("BASE EM COLAPSO!", 1.0f, 0.0f, 0.0f);
        // Leak resources rapidly
        g_base_oxygen = std::max(0.0f, g_base_oxygen - 5.0f * dt);
        g_base_water = std::max(0.0f, g_base_water - 3.0f * dt);
        // Damage player if at base
        if (at_base) {
            g_player.hp = std::max(0, g_player.hp - 1);
        }
    } else if (g_base_integrity < 30.0f) {
        add_alert("Integridade critica - Construa Oficina!", 1.0f, 0.5f, 0.3f);
    }

    // ========== NATURAL PROCESSES ==========

    // Natural temperature equilibrium
    float base_temp = -60.0f + g_co2_level * 0.8f;
    g_temperature = lerp(g_temperature, base_temp, 0.001f * dt);

    // Player suit consumption (outside base uses suit tanks faster; submerso gasta bem mais
    // rapido - equivale ao "medidor de folego" debaixo d'agua, reaproveitando o O2 do traje
    // ja existente em vez de criar um stat novo so pra isso).
    float suit_use_mult = at_base ? 0.3f : 1.0f;  // Use less when at base
    if (g_physics.submerged) suit_use_mult *= 6.0f;
    // Traje "falhado" (g_suit_integrity zerado, ver main.cpp) amplifica o dreno de
    // O2/agua/comida - consequencia real mas nao letal na hora, empurra reparo (tecla F)
    // sem matar o jogador direto so por deixar o traje se degradar.
    if (g_suit_integrity <= 0.0f) suit_use_mult *= 1.75f;
    float suit_o2_use = 0.12f * suit_use_mult * dt;
    float suit_water_use = 0.06f * suit_use_mult * dt;
    float suit_food_use = 0.03f * suit_use_mult * dt;

    g_player_oxygen = std::max(0.0f, g_player_oxygen - suit_o2_use);
    g_player_water = std::max(0.0f, g_player_water - suit_water_use);
    g_player_food = std::max(0.0f, g_player_food - suit_food_use);

    // Sync legacy variables for compatibility
    g_oxygen = g_player_oxygen;
    g_water_res = g_player_water;
    g_food = g_player_food;
    g_energy = g_base_energy;

    // HP regeneration when well fed (faster regeneration)
    if (g_player_food > 40.0f && g_player.hp < 100) {
        static float regen_timer = 0.0f;
        regen_timer += dt;
        // Regenerate 2 HP every 1.2 seconds (was 1 HP every 2s)
        if (regen_timer >= 1.2f) {
            regen_timer = 0.0f;
            int regen_amount = (g_player_food > 75.0f) ? 3 : 2;  // More food = faster regen
            g_player.hp = std::min(100, g_player.hp + regen_amount);
        }
    }

    // Update phase based on current conditions
    update_phase();
    update_objectives(dt);

    // Melt ice globally when temperature rises above freezing
    static float melt_timer = 0.0f;
    melt_timer += dt;
    if (melt_timer >= 2.0f && g_temperature >= kTempThawing) {
        melt_timer = 0.0f;
        // Randomly melt some ice blocks
        for (int i = 0; i < 10; ++i) {
            int x = rng_next_u32() % world.w;
            int y = rng_next_u32() % world.h;
            if (world.get(x, y) == Block::Ice) {
                world.set(x, y, Block::Water);
                g_surface_dirty = true;
            }
        }
    }
}
