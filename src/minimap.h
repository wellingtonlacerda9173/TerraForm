#pragma once

// ============= Minimap / World Map / Waypoints =============
// Extracted verbatim from main.cpp (original lines ~846-1399): fog-of-war tracking,
// waypoint management, and the two minimap renderers (corner HUD minimap + fullscreen
// world map). All six functions lost "static": main.cpp's update_game()/render_world()
// (out of scope for this stage) still call all of them - same pattern as g_world/g_camera
// in earlier extraction stages. get_minimap_color() is NOT declared here on purpose - it
// is only ever used internally by render_minimap()/render_world_map() (both moved here
// too), so it stays file-local (static) to minimap.cpp.
void update_fog_of_war();
void add_waypoint(int x, int y, const char* label = nullptr);
void remove_nearest_waypoint(int x, int y);
void clear_all_waypoints();
void render_minimap(int win_w, int win_h);
void render_world_map(int win_w, int win_h);
