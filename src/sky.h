#pragma once

// ============= Ceu Alienigena (esferas reais + parallax) =============
// Extracted verbatim from main.cpp (original lines ~1329-1755): the SkyPalette struct, the
// day/night sky palette computation, and the full sky renderer (gradient dome, stars,
// nebula, clouds, sun/moons as lit spheres, shooting stars) - render_alien_sky() ties all
// of it together and is the one entry point main.cpp's render_world() calls.
//
// SkyPalette needs a complete definition here (not just a forward declaration): main.cpp's
// render_world() (out of scope for this stage, untouched) declares its own
// "SkyPalette sky_palette = compute_sky_palette(...)" local and reads its hz_*/zn_* fields
// directly to pick the GL clear color - same "owner keeps the definition" rule used for
// Module/ModuleStatus in modules_building.h.
struct SkyPalette {
    float hz_r = 0.0f, hz_g = 0.0f, hz_b = 0.0f;
    float zn_r = 0.0f, zn_g = 0.0f, zn_b = 0.0f;
};

// compute_sky_palette()/render_alien_sky() are called directly by main.cpp's render_world()
// (out of scope for this stage, untouched), so both stay non-static and are declared here.
// update_shooting_stars() was already non-static before this stage (modules_building.cpp's
// update_modules() calls it from another translation unit); it is declared here now that
// this header is its proper home, but modules_building.cpp keeps working unchanged either
// way since it already carries its own matching forward declaration.
//
// hash01() and the rest of the layer renderers (render_sky_gradient_dome,
// render_billboard_disc, render_lit_sphere, render_star_layer, render_nebula_layer,
// render_cloud_layer, render_shooting_stars) are NOT declared here: grep confirms every call
// site is inside render_alien_sky() itself (or one of these helpers calling another), all in
// this same cluster, so they stay static in sky.cpp - same pattern as get_minimap_color() in
// minimap.cpp.
SkyPalette compute_sky_palette(float day_phase, float atmos_factor);
void update_shooting_stars(float dt, float day_phase);
void render_alien_sky(float cam_x, float cam_y, float cam_z, float day_phase, float atmos_factor);
