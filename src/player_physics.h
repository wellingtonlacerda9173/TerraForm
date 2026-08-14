#pragma once

#include "math_core.h"

#include <array>
#include <cstdint>
#include <string>

// ============= Player / Physics =============
// Extracted verbatim from main.cpp (original lines ~215-317, 1116-1296, 1942-1958,
// 1978-2692): the Player struct + g_player instance, the physics-runtime types
// (TerrainPhysicsType/PhysicsRayDebug/PhysicsRuntime/PlayerPhysicsInput) + g_physics
// instance, the fixed-timestep collision/movement solver (probe_ground,
// try_step_climb, apply_single_physics_step, step_player_physics, etc. — these stay
// `static` inside player_physics.cpp since nothing outside calls them directly), and
// the spawn/respawn functions (spawn_player_at_base, respawn_player_at_base,
// spawn_player_new_game) plus the small approach() easing helper.
//
// This is the last of the world -> camera -> player_physics extraction sequence:
// player_physics.cpp includes world.h (World/g_world, surface_height_at, etc.) and
// camera.h (reset_camera_near_player) as needed, but this header does NOT include
// camera.h — only a forward declaration of World is needed here (functions take it
// by reference), matching the same "owner keeps the definition, users just
// forward-declare or extern" rule used for the world/camera split.
struct World;

struct Player {
    Vec2 pos = {0.0f, 0.0f}; // tile units (X, Z no espaco 3D)
    Vec2 vel = {0.0f, 0.0f}; // tiles/sec (horizontal)
    float w = 0.60f;
    float h = 0.60f;  // Tamanho de colisao (menor para caber entre blocos)
    int hp = 100;
    
    // === SISTEMA 3D - ALTURA E PULO ===
    float pos_y = 1.0f;       // Altura atual (Y no espaco 3D)
    float vel_y = 0.0f;       // Velocidade vertical
    bool on_ground = false;   // Se esta no chao
    bool can_jump = true;     // Pode pular (evita pulo infinito)
    float ground_height = 0.0f; // Altura do chao sob o jogador
    
    // Rotacao continua (graus, 0 = Norte, 90 = Leste, 180 = Sul, 270 = Oeste)
    float rotation = 180.0f;       // Rotacao atual
    float target_rotation = 180.0f; // Rotacao alvo (para suavizacao)
    
    // Compatibilidade com sistema antigo (calculado a partir de rotation)
    int facing_dir = 2;      // 0=Norte, 1=Leste, 2=Sul, 3=Oeste
    
    float walk_timer = 0.0f; // For walk animation
    float anim_frame = 0.0f; // General animation counter
    bool is_mining = false;
    float mine_anim = 0.0f;
    bool is_moving = false;  // Se esta andando
    
    // === JETPACK ===
    bool jetpack_active = false;  // Jetpack esta ativo
    float jetpack_fuel = 100.0f;  // Combustivel do jetpack (0-100)
    float jetpack_flame_anim = 0.0f; // Animacao da chama
    
    // Movimento suave
    float speed_mult = 1.0f;  // Multiplicador de velocidade (acelera gradualmente)
};

// O unico Player do jogo. Definido (nao-static) em player_physics.cpp; main.cpp continua
// acessando g_player diretamente (render, UI, mineracao, save/load etc.) atraves desta
// declaracao extern - mesmo padrao de g_world/g_camera nos estagios anteriores.
extern Player g_player;

enum class TerrainPhysicsType : uint8_t {
    Normal = 0,
    Ice,
    Sand,
    Stone,
    Mud,
};

struct PhysicsRayDebug {
    Vec3 from = {0.0f, 0.0f, 0.0f};
    Vec3 to = {0.0f, 0.0f, 0.0f};
    bool hit = false;
};

struct PhysicsRuntime {
    float accumulator = 0.0f;
    float alpha = 0.0f;

    Vec2 prev_pos = {0.0f, 0.0f};
    float prev_pos_y = 0.0f;
    float prev_rotation = 180.0f;

    Vec2 render_pos = {0.0f, 0.0f};
    float render_pos_y = 0.0f;
    float render_rotation = 180.0f;

    float jump_buffer_timer = 0.0f;
    float coyote_timer = 0.0f;
    bool jump_was_held = false;

    bool stepped = false;
    bool hit_x = false;
    bool hit_z = false;
    bool sliding = false;
    TerrainPhysicsType terrain = TerrainPhysicsType::Normal;
    std::string terrain_name = "Normal";
    Vec3 ground_normal = {0.0f, 1.0f, 0.0f};
    Vec2 collision_normal = {0.0f, 0.0f};

    std::array<PhysicsRayDebug, 8> debug_rays = {};
    int debug_ray_count = 0;
};

struct PlayerPhysicsInput {
    Vec2 move = {0.0f, 0.0f};
    bool has_move = false;
    bool run = false;
    bool jump_pressed = false;
    bool jump_held = false;
    bool jump_released = false;
};

// O unico PhysicsRuntime do jogo. Definido (nao-static) em player_physics.cpp; main.cpp
// continua lendo g_physics diretamente (HUD de debug em F3, desenho dos raios de debug,
// leitura de jump_was_held no polling de input) atraves desta declaracao extern - so
// perdeu o "static" que tinha em main.cpp porque agora e lida de duas unidades de
// traducao (mesmo padrao de g_camera_mode/g_camera_debug_rays em camera.h).
extern PhysicsRuntime g_physics;

void reset_player_physics_runtime(bool clear_timers = true);
void step_player_physics(const PlayerPhysicsInput& input, float frame_dt);

// Easing helper generico (aproxima "cur" de "target" em no maximo "max_delta"). Usado
// pelo solver de fisica acima e tambem pelo smoothing de g_cam_pos em main.cpp.
float approach(float cur, float target, float max_delta);

// ============= Spawn / Respawn =============
void spawn_player_at_base();
void respawn_player_at_base(const char* death_reason);
void spawn_player_new_game(World& world);

// Definidas em player_physics.cpp (leem g_physics.render_pos/render_pos_y); camera.cpp
// (e o resto de main.cpp) continuam chamando-as atraves desta declaracao. camera.h ja
// forward-declara as duas de forma identica (nao gera conflito - duplicated non-
// conflicting extern declarations across TUs sao permitidas em C++); mantidas aqui
// tambem, ja que este e o modulo dono da definicao agora.
Vec2 get_player_render_pos();
float get_player_render_y();
