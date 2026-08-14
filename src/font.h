#pragma once

#include "platform.h"

#include <string>

// ============= Font =============
// Extracted verbatim from main.cpp (original lines ~413-443): the tiny GDI/OpenGL
// bitmap-font helper used by all on-screen text (HUD, menus, minimap labels, etc.).
// init_font()/draw_text()/estimate_text_w_px() lost "static": main.cpp (render_world/
// update_game, out of scope for this stage) and the new minimap.cpp both call draw_text()/
// estimate_text_w_px() from other translation units now - same pattern as g_world/g_camera
// in earlier extraction stages. init_font() is only ever called once from main.cpp
// (WinMain's GL context setup), but it needs external linkage too since it now lives in a
// different translation unit than its caller.
void init_font(HDC hdc);
void draw_text(float x, float y, const std::string& s, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
float estimate_text_w_px(const std::string& s);
