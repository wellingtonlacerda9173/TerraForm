#pragma once

#include "platform.h"   // GLuint, GL/gl.h types used by g_tex_atlas / init_texture_atlas
#include "blocks.h"      // Block

// ============= TEXTURAS (ESTILO MINICRAFT / PIXEL ART) =============
// Extracted verbatim from main.cpp (original lines ~528-1017).
// Sem assets externos: atlas gerado proceduralmente em tempo de execucao.

enum class Tile : int {
    Missing = 0,
    // Naturais
    GrassTop,
    GrassSide,
    Dirt,
    Stone,
    Sand,
    Water0,
    Water1,
    Water2,
    Water3,
    Ice,
    Snow,
    WoodTop,
    WoodSide,
    Leaves,
    // Recursos
    CoalOre,
    IronOre,
    CopperOre,
    CrystalOre,
    Metal,
    Organic,
    Components,
    // Modulos
    SolarPanel,
    EnergyGenerator,
    WaterExtractor,
    OxygenGenerator,
    Greenhouse,
    CO2Factory,
    Habitat,
    Workshop,
    Terraformer,
    // Estruturas base
    RocketHull,
    RocketEngine,
    RocketWindow,
    RocketNose,
    RocketFin,
    RocketDoor,
    DomeGlass,
    DomeFrame,
    LandingPad,
    BuildSlot,
    Pipe,
    Antenna,
    // Cracks (mining)
    Crack1,
    Crack2,
    Crack3,
    Crack4,
    Crack5,
    Crack6,
    Crack7,
    Crack8,
};

struct UvRect {
    float u0, v0, u1, v1;
};

UvRect atlas_uv(Tile t);

void init_texture_atlas();

extern GLuint g_tex_atlas;

struct BlockTex {
    Tile top = Tile::Missing;
    Tile side = Tile::Missing;
    Tile bottom = Tile::Missing;
    bool uses_tint = false;      // Se true, multiplicar textura por block_color() (vida/atmosfera)
    bool transparent = false;    // Se true, respeitar alpha do block_color()
    bool is_water = false;       // Para pequenas regras de render (altura/anim)
};

BlockTex block_tex(Block b);

// ============= Block Colors =============
void block_color(Block b, int y, int world_h, float& r, float& g, float& bl, float& a);
