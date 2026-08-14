#include "font.h"

// g_font_base lost its "static" *file-scope* visibility only in the sense that it moved
// translation units - it keeps internal linkage here (still `static`), exactly as it had
// in main.cpp: only the three functions in this file touch it (verified via grep across
// src/ before this extraction), so it has no reason to be exposed via extern.
static GLuint g_font_base = 0;

void init_font(HDC hdc) {
    if (g_font_base != 0) return;
    HFONT font = CreateFontA(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        FF_DONTCARE | DEFAULT_PITCH, "Consolas");
    if (!font) return;

    HGDIOBJ old = SelectObject(hdc, font);
    g_font_base = glGenLists(96);
    wglUseFontBitmapsA(hdc, 32, 96, g_font_base);
    SelectObject(hdc, old);
    DeleteObject(font);
}

void draw_text(float x, float y, const std::string& s, float r, float g, float b, float a) {
    if (g_font_base == 0 || s.empty()) return;
    glColor4f(r, g, b, a);
    glRasterPos2f(x, y);
    glPushAttrib(GL_LIST_BIT);
    glListBase(g_font_base - 32);
    glCallLists((GLsizei)s.size(), GL_UNSIGNED_BYTE, s.c_str());
    glPopAttrib();
}

float estimate_text_w_px(const std::string& s) {
    return (float)s.size() * 8.0f;
}
