cmake_minimum_required(VERSION 3.16)
project(TerraFormer2D LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# raylib (modernizacao: substitui Win32 + OpenGL 1.x fixed-function, etapa por etapa -
# ver plano em C:\Users\9173\.claude\plans\quero-refatorar-todo-o-serene-kazoo.md).
# Etapa 1 (scaffolding): so busca/compila a raylib e linka no executavel existente, sem
# nenhuma chamada de API da raylib ainda - o jogo continua 100% Win32/GL ate a proxima etapa.
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG 5.5
)
FetchContent_MakeAvailable(raylib)

# Win32 + OpenGL (legado, sendo substituido)
add_executable(terraformer WIN32
  src/main.cpp
  src/math_core.cpp
  src/noise.cpp
  src/blocks.cpp
  src/textures.cpp
  src/config_io.cpp
  src/game_state.cpp
  src/world.cpp
  src/camera.cpp
  src/player_physics.cpp
  src/items_particles.cpp
  src/inventory_crafting.cpp
  src/modules_building.cpp
  src/font.cpp
  src/save_load.cpp
  src/minimap.cpp
  src/render_primitives.cpp
  src/render_player.cpp
  src/lighting.cpp
  src/sky.cpp
  src/ui_hud.cpp
  src/ui_menu.cpp
  src/building_interaction.cpp
  src/input.cpp
  src/win32_platform.cpp
  src/objectives.cpp
)

if(MSVC)
  target_compile_definitions(terraformer PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(terraformer PRIVATE opengl32 gdi32 user32 kernel32 raylib)
else()
  target_link_libraries(terraformer PRIVATE GL X11 raylib)
endif()

message(STATUS "Configured TerraFormer2D - Win32 Standalone")
