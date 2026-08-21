#pragma once

#include <cstdint>

// ============= Blocks =============
// Extracted verbatim from main.cpp (original lines ~195-380).
enum class Block : uint8_t {
    Air = 0,
    Grass,
    Dirt,
    Stone,
    Sand,
    Water,
    Ice,           // Frozen water (before warming)
    Snow,          // Snow (top-down biomes)
    Wood,
    Leaves,
    Coal,
    Iron,
    Copper,        // New resource for advanced modules
    Crystal,       // Rare crystal for energy systems
    Metal,         // Refined metal
    Organic,       // Organic material for food/plants
    Components,    // Electronic components
    // Modules
    SolarPanel,
    EnergyGenerator,  // Main power source
    WaterExtractor,
    OxygenGenerator,
    Greenhouse,    // Food production
    CO2Factory,    // Releases CO2 for warming
    Habitat,       // Living quarters
    Workshop,      // Repairs and crafting
    TerraformerBeacon,
    // Base structures (not buildable, generated)
    RocketHull,    // Landed rocket
    RocketEngine,  // Rocket engine
    RocketWindow,  // Rocket window
    RocketNose,    // Rocket nose cone
    RocketFin,     // Rocket fins
    RocketDoor,    // Rocket door/hatch
    DomeGlass,     // Habitat dome glass
    DomeFrame,     // Dome metal frame
    LandingPad,    // Landing pad floor
    BuildSlot,     // Empty slot for building modules
    PipeH,         // Horizontal pipe
    PipeV,         // Vertical pipe
    Antenna,       // Communication antenna
    // Anexado no FIM de proposito: Block e salvo como uint8_t bruto (save_load.cpp), entao
    // inserir no meio do enum reordenaria (corromperia) saves existentes.
    Lava,          // Fundo de cratera vulcanica - puramente cosmetico, sem fluxo/dano
    // Idem: anexado apos Lava (fim real do enum). Recurso de inventario puro (like Metal/
    // Components), nunca colocado como tile do mundo - so existe em g_inventory, produzido
    // via try_refine_at_workshop() (modules_building.cpp, tecla G).
    RefinedAlloy,  // Liga metalica refinada (Oficina) - da a Metal seu 2o uso de verdade
    // Idem: anexado apos RefinedAlloy (fim real do enum). Item de posse pura (0 ou 1 em
    // g_inventory, nunca colocado como tile do mundo) - fabricado 1x na Oficina via
    // try_craft_laser_pistol() (modules_building.cpp, tecla P), selecionavel no hotbar pra
    // atirar em criaturas (creatures.cpp).
    LaserPistol,   // Pistola de laser
};

static constexpr int kBlockTypeCount = (int)Block::LaserPistol + 1;

bool is_transparent(Block b);

// Top-down: tiles que bloqueiam movimento (inverso de walkable)
bool is_solid(Block b);

bool is_module(Block b);

bool is_base_structure(Block b);

// Blocos que representam "solo/superficie" (nao sao objetos acima do terreno).
// Usado para separar terreno (ground) de objetos (rochas, minerios, modulos, etc).
bool is_ground_like(Block b);

// Top-down: tiles que permitem movimento do jogador
bool is_walkable(Block b);

const char* block_name(Block b);

// ============= Terraforming Phases =============
enum class TerraPhase {
    Frozen = 0,      // Starting phase: -60°C, no liquid water, need suits
    Warming,         // CO2 being released, temperature rising
    Thawing,         // 0°C+, ice melting, liquid water possible
    Habitable,       // 15°C+, can plant outside, atmosphere forming
    Terraformed,     // Earth-like conditions achieved!
};

const char* phase_name(TerraPhase p);
