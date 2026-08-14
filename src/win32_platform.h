#pragma once

// ============= Win32 Platform Layer (Window Procedure / WinMain) =============
// Extracted verbatim from main.cpp (original lines ~2184-2385): the Win32 window
// procedure and the WinMain entry point that owns window creation, the OpenGL context
// setup, config loading, and the main message/update/render loop. This is the LAST
// extraction stage of the whole refactor plan (Fase 1 / foundation) - after this,
// main.cpp contains only includes, a handful of leftover globals that don't cleanly
// belong to any single subsystem, and the two intentional final orchestrators
// (render_world/update_game).
//
// This header intentionally declares nothing: WinMain is the OS-designated process entry
// point (the WIN32 subsystem linker/CRT finds it by name/signature, not via a declaration
// anywhere in this codebase), and WindowProc is referenced by exactly one line
// ("wc.lpfnWndProc = WindowProc;") inside WinMain itself, in the same .cpp - grep across
// all of src/ confirms no other file references either symbol - so WindowProc stays
// `static` (internal linkage) to win32_platform.cpp, same as before the move. The two
// small mouse-camera-control globals (g_last_mouse_x/g_last_mouse_y/g_mouse_captured) and
// the OpenGL context setup helper (setup_opengl) move into win32_platform.cpp too, for the
// same reason: grep confirms WindowProc/WinMain are their only readers/writers/callers
// respectively, so they all stay file-local statics there instead of gaining extern
// linkage.
