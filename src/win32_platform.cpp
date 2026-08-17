#include "win32_platform.h"

#include "raylib_platform.h"
#include "math_core.h"
#include "world.h"
#include "camera.h"
#include "player_physics.h"
#include "game_state.h"
#include "config_types.h"
#include "config_io.h"
#include "textures.h"
#include "font.h"

// ============= Platform Layer (raylib main loop) =============
// Migrated from Win32 (WindowProc/WinMain/WGL context setup) to raylib's InitWindow()/
// WindowShouldClose()/CloseWindow(). See win32_platform.h for the high-level description.

// g_quit/g_cam_pos/g_mouse_x/g_mouse_y/g_mouse_left_clicked/g_minimap/g_map_cfg are owned by
// main.cpp (already non-static there for other extracted modules) - this file gets them via
// its own local extern declarations, same pattern used throughout this codebase's extraction
// stages. g_state/g_camera/g_player/g_world/g_physics_cfg-style globals are NOT re-declared
// here: game_state.h/camera.h/player_physics.h/world.h already supply real (non-forward)
// extern declarations for g_state/g_camera/g_player/g_world.
extern bool g_quit;
extern Vec2 g_cam_pos;
extern int g_mouse_x;
extern int g_mouse_y;
extern bool g_mouse_left_clicked;
extern MiniMapRuntime g_minimap;
extern MapConfig g_map_cfg;

// WORLD_WIDTH/WORLD_HEIGHT: compile-time literals (not mutable state) defined in main.cpp;
// kept here as this file's own copy rather than shared via extern - same pattern as elsewhere
// in this codebase.
static const int WORLD_WIDTH = 512;
static const int WORLD_HEIGHT = 256;

// render_world()/update_game() stay DEFINED in main.cpp per the refactor plan - they are the
// two intentional final orchestrators left there. They lost their HWND/HDC parameters in this
// stage (raylib has no window handle/device context to hand them) - this file forward-declares
// the new signatures itself, mirroring the "own local declaration" pattern used throughout this
// codebase's extraction stages.
void render_world(int win_w, int win_h);
void update_game(float dt);

// ============= Mouse / keyboard polling (raylib) =============
// Migrated from WindowProc's WM_MOUSEWHEEL/WM_MBUTTONDOWN/WM_MBUTTONUP/WM_MOUSEMOVE/
// WM_LBUTTONDOWN/WM_RBUTTONDOWN handlers, which used to run per Win32 message; raylib has no
// message loop, so this runs once per frame instead, polling raylib's input state directly.
// The WM_KEYDOWN handler's "ESC quits from the Menu state" branch was NOT ported: it was
// already fully duplicated by ui_menu.cpp's update_menu_input() Menu-state block
// ("if (esc_pressed) { g_quit = true; return true; }"), which update_game() still calls every
// frame - so dropping the WindowProc copy loses no behavior.
static bool g_mouse_captured = false;

static void process_input_events() {
    // Mouse wheel: raylib's GetMouseWheelMove() returns ~1.0 per notch, versus Win32's
    // GET_WHEEL_DELTA_WPARAM which returned ~120 per notch - the old 0.001f (map zoom) and
    // 0.005f (camera distance) multipliers are rescaled ~120x here (0.001*120=0.12,
    // 0.005*120=0.6) to preserve the same real-world zoom/distance change per wheel notch.
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        if (g_minimap.world_map_open) {
            float zoom_delta = wheel * 0.12f;
            g_minimap.world_zoom += zoom_delta;
            g_minimap.world_zoom = std::clamp(g_minimap.world_zoom,
                g_map_cfg.world_map_zoom_min, g_map_cfg.world_map_zoom_max);
        } else {
            g_camera.distance -= wheel * 0.6f;
            g_camera.distance = std::clamp(g_camera.distance, g_camera.min_distance, g_camera.max_distance);
        }
    }

    // Middle mouse button: capture the cursor to rotate the camera, exactly like the old
    // SetCapture/ShowCursor(FALSE) pair - DisableCursor()/EnableCursor() also hides the OS
    // cursor and (on desktop platforms) keeps it centered, which is what let the original code
    // compute a mouse delta by re-centering every frame; GetMouseDelta() gives that delta
    // directly now, no manual recentring math needed.
    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        g_mouse_captured = true;
        DisableCursor();
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE)) {
        g_mouse_captured = false;
        EnableCursor();
    }

    if (g_mouse_captured && g_state == GameState::Playing) {
        Vector2 delta = GetMouseDelta();
        // Rotacionar camera (mouse direita = camera gira direita) - mesma matematica de
        // sensitivity de antes.
        g_camera.yaw += delta.x * g_camera.sensitivity;
        g_camera.pitch -= delta.y * g_camera.sensitivity * 0.5f;

        g_camera.pitch = std::clamp(g_camera.pitch, g_camera.min_pitch, g_camera.max_pitch);

        while (g_camera.yaw >= 360.0f) g_camera.yaw -= 360.0f;
        while (g_camera.yaw < 0.0f) g_camera.yaw += 360.0f;
    }

    // Sempre atualiza posicao do mouse (equivalente ao antigo WM_MOUSEMOVE, que sempre
    // atualizava g_mouse_x/g_mouse_y mesmo fora da captura de camera).
    Vector2 mouse_pos = GetMousePosition();
    g_mouse_x = (int)mouse_pos.x;
    g_mouse_y = (int)mouse_pos.y;

    // Clique esquerdo/direito - flags de single-frame consumidos por update_game()/render_hud().
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        g_mouse_left_clicked = true;
    }
}

// ============= main() =============
int main() {
    // FLAG_WINDOW_RESIZABLE precisa ser setado ANTES de InitWindow() - por padrao a raylib
    // cria uma janela de tamanho fixo (diferente da janela Win32 original, que era
    // redimensionavel via WS_OVERLAPPEDWINDOW).
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1280, 720, "TerraFormer 2D");
    SetWindowMinSize(640, 360);

    // Atlas pixel-art (Minicraft/Minecraft-like) + fonte padrao da raylib.
    init_texture_atlas();
    init_font();

    reload_physics_config(true);
    reload_terrain_config(true);
    reload_sky_config(true);
    reload_camera_config(true);
    reload_mining_config(true);
    reload_player_visual_config(true);

    // Initialize world for menu background
    g_world = new World(WORLD_WIDTH, WORLD_HEIGHT, 1337);
    spawn_player_new_game(*g_world);
    g_cam_pos = g_player.pos;
    g_state = GameState::Menu;

    // Main loop - raylib's own GetFrameTime() replaces the old manual QueryPerformanceCounter
    // delta-time timing (idiomatic raylib, removes another chunk of Win32-specific code).
    while (!WindowShouldClose() && !g_quit) {
        float dt = std::clamp(GetFrameTime(), 0.0001f, 0.1f); // Clamp to avoid huge jumps

        process_input_events();

        // Update
        update_game(dt);

        // Render
        BeginDrawing();
        render_world(GetScreenWidth(), GetScreenHeight());
        EndDrawing();
    }

    // Cleanup
    delete g_world;
    g_world = nullptr;

    CloseWindow();
    return 0;
}
