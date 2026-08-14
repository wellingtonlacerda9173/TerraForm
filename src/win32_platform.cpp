#include "win32_platform.h"

#include "platform.h"
#include "math_core.h"
#include "world.h"
#include "camera.h"
#include "player_physics.h"
#include "game_state.h"
#include "config_types.h"
#include "config_io.h"
#include "textures.h"
#include "font.h"

// ============= Win32 Platform Layer (Window Procedure / WinMain) =============
// Extracted verbatim from main.cpp - see win32_platform.h for the full extraction-stage
// description (this is the last stage of the whole refactor plan).

// g_quit/g_cam_pos/g_mouse_x/g_mouse_y/g_mouse_left_clicked/g_minimap/g_map_cfg are owned
// by main.cpp (already non-static there for other extracted modules) - this file gets them
// via its own local extern declarations, same "own local extern declaration" pattern used
// by every other extracted .cpp file (e.g. g_show_build_menu/etc. in building_interaction.cpp,
// g_settings/etc. in ui_menu.cpp). g_state/g_camera/g_player/g_world/g_physics_cfg-style
// globals are NOT re-declared here: game_state.h/camera.h/player_physics.h/world.h already
// supply real (non-forward) extern declarations for g_state/g_camera/g_player/g_world.
extern bool g_quit;
extern Vec2 g_cam_pos;
extern int g_mouse_x;
extern int g_mouse_y;
extern bool g_mouse_left_clicked;
extern MiniMapRuntime g_minimap;
extern MapConfig g_map_cfg;

// WORLD_WIDTH/WORLD_HEIGHT: compile-time literals (not mutable state) defined in main.cpp;
// kept here as this file's own copy rather than shared via extern - same pattern as the
// WORLD_WIDTH/WORLD_HEIGHT duplication already used in ui_menu.cpp (and the kDayLength-style
// duplication in modules_building.cpp/minimap.cpp/sky.cpp/lighting.cpp/ui_hud.cpp).
static const int WORLD_WIDTH = 512;
static const int WORLD_HEIGHT = 256;

// render_world()/update_game() stay DEFINED in main.cpp per the refactor plan - they are
// the two intentional final orchestrators left there, not part of this extraction. They
// lose "static" linkage in main.cpp as of this stage, since WinMain (below, now in this
// different translation unit) is their only caller. No shared header declares them (they
// are not a reusable module, just the last two functions left in main.cpp), so this file
// forward-declares them itself, mirroring the "own local declaration" pattern used
// throughout this codebase's extraction stages.
void render_world(HDC hdc, int win_w, int win_h);
void update_game(float dt, HWND hwnd);

// ============= OpenGL Setup =============
static HGLRC setup_opengl(HDC hdc) {
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int format = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, format, &pfd);

    HGLRC hrc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, hrc);

    glDisable(GL_DEPTH_TEST);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Atlas pixel-art (Minicraft/Minecraft-like)
    init_texture_atlas();
    glBindTexture(GL_TEXTURE_2D, 0);
    return hrc;
}

// ============= Window Procedure =============
// Variaveis para controle de camera com mouse
static int g_last_mouse_x = 0;
static int g_last_mouse_y = 0;
static bool g_mouse_captured = false;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            g_quit = true;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && g_state == GameState::Menu) {
                g_quit = true;
            }
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);

            // Se mapa grande esta aberto, controlar zoom do mapa
            if (g_minimap.world_map_open) {
                float zoom_delta = (float)delta * 0.001f;
                g_minimap.world_zoom += zoom_delta;
                g_minimap.world_zoom = std::clamp(g_minimap.world_zoom,
                    g_map_cfg.world_map_zoom_min, g_map_cfg.world_map_zoom_max);
            } else {
                // Zoom da camera com scroll do mouse
                g_camera.distance -= (float)delta * 0.005f;
                g_camera.distance = std::clamp(g_camera.distance, g_camera.min_distance, g_camera.max_distance);
            }
            return 0;
        }
        case WM_MBUTTONDOWN:
            // Capturar mouse ao clicar com botao do meio para rotacionar camera
            g_mouse_captured = true;
            SetCapture(hwnd);
            ShowCursor(FALSE);
            return 0;
        case WM_MBUTTONUP:
            // Liberar mouse
            g_mouse_captured = false;
            ReleaseCapture();
            ShowCursor(TRUE);
            return 0;
        case WM_RBUTTONDOWN:
            // Clique direito do mouse - usado para construir (processado no update)
            g_mouse_x = LOWORD(lParam);
            g_mouse_y = HIWORD(lParam);
            return 0;
        case WM_LBUTTONDOWN:
            // Clique esquerdo do mouse - usado para selecionar/minerar
            g_mouse_left_clicked = true;
            g_mouse_x = LOWORD(lParam);
            g_mouse_y = HIWORD(lParam);
            return 0;
        case WM_MOUSEMOVE:
            // Sempre atualiza posicao do mouse
            g_mouse_x = LOWORD(lParam);
            g_mouse_y = HIWORD(lParam);

            if (g_mouse_captured && g_state == GameState::Playing) {
                int mx = LOWORD(lParam);
                int my = HIWORD(lParam);

                int delta_x = mx - g_last_mouse_x;
                int delta_y = my - g_last_mouse_y;

                // Rotacionar camera (mouse direita = camera gira direita)
                g_camera.yaw += delta_x * g_camera.sensitivity;
                g_camera.pitch -= delta_y * g_camera.sensitivity * 0.5f;

                // Clamp pitch
                g_camera.pitch = std::clamp(g_camera.pitch, g_camera.min_pitch, g_camera.max_pitch);

                // Normalizar yaw
                while (g_camera.yaw >= 360.0f) g_camera.yaw -= 360.0f;
                while (g_camera.yaw < 0.0f) g_camera.yaw += 360.0f;

                // Recentrar o mouse
                RECT rc;
                GetClientRect(hwnd, &rc);
                POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
                ClientToScreen(hwnd, &center);
                SetCursorPos(center.x, center.y);

                ScreenToClient(hwnd, &center);
                g_last_mouse_x = center.x;
                g_last_mouse_y = center.y;
            } else {
                g_last_mouse_x = LOWORD(lParam);
                g_last_mouse_y = HIWORD(lParam);
            }
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ============= WinMain =============
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Register window class
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "TerraFormer2DClass";

    if (!RegisterClassA(&wc)) {
        MessageBoxA(nullptr, "Failed to register window class", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create window
    int win_w = 1280;
    int win_h = 720;
    RECT wr = {0, 0, win_w, win_h};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowA(
        "TerraFormer2DClass",
        "TerraFormer 2D",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) {
        MessageBoxA(nullptr, "Failed to create window", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    HDC hdc = GetDC(hwnd);
    HGLRC hrc = setup_opengl(hdc);
    init_font(hdc);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

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

    // Timing
    LARGE_INTEGER freq, last_time, cur_time;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last_time);

    // Main loop
    MSG msg;
    while (!g_quit) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (g_quit) break;

        // Calculate delta time
        QueryPerformanceCounter(&cur_time);
        float dt = (float)(cur_time.QuadPart - last_time.QuadPart) / (float)freq.QuadPart;
        last_time = cur_time;
        dt = std::clamp(dt, 0.0001f, 0.1f); // Clamp to avoid huge jumps

        // Update
        update_game(dt, hwnd);

        // Render
        RECT rc;
        GetClientRect(hwnd, &rc);
        render_world(hdc, rc.right - rc.left, rc.bottom - rc.top);

        // Small sleep to avoid 100% CPU
        Sleep(1);
    }

    // Cleanup
    delete g_world;
    g_world = nullptr;

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(hrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);

    return 0;
}
