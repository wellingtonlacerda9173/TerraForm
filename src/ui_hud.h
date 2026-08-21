#pragma once

// ============= HUD Rendering =============
// Extracted verbatim from main.cpp's render_world() (original lines ~1707-2451): the
// switch from 3D to 2D/ortho projection, the vignette effect, the lightmap/lights debug
// overlays, the mouse crosshair, HP/O2/water/food/jetpack status bars, base resource bars,
// terraforming/phase/temperature/CO2/atmosphere stats, the base direction indicator, the
// minimap, the hotbar (resource + module slots), collect popups, target/debug info, toast
// notifications, screen-flash feedback, the unlock popup, and the onboarding tip.
//
// render_hud() lost "static" by construction: it is defined here and called from
// render_world() in main.cpp, which is a different translation unit - same pattern as
// every other render_*()/update_*() entry point declared in the other extracted headers.
//
// Only the projection switch + HUD drawing moved to this stage; the Paused/Menu/Dead/
// Settings overlay block and the alerts/world-map overlay that used to sit right after
// this in render_world() stay inline in main.cpp (a later ui_menu extraction stage
// handles those).
void render_hud(int win_w, int win_h);

// Geometria do painel direito (Fase/terraformacao) - exposta pra outros arquivos (hoje so
// minimap.cpp) poderem ancorar elementos em relacao a ele sem duplicar os mesmos numeros
// magicos numa 2a copia independente. Bug real corrigido por causa disso: o minimapa
// ancorava numa altura fixa (200px) que nao tinha ligacao nenhuma com a altura de verdade
// deste painel (calculada aqui) - qualquer mudanca num dos dois lados descolava do outro,
// e chegou a sobrepor. render_hud() usa essas mesmas funcoes internamente agora, entao os
// dois nunca mais podem divergir.
float hud_right_panel_right_x(int win_w);
float hud_right_panel_bottom_y();
