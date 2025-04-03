
add_library(terrain_generator TerrainGeneratorModule.cpp)

target_include_directories(terrain_generator PUBLIC ..)

target_include_directories(terrain_generator INTERFACE shaders)
# Allow GLSL code to include helper files and compat
target_shader_include_directories(terrain_generator INTERFACE shaders)

target_link_libraries(terrain_generator PUBLIC etna render_utils gui)


target_add_shaders(terrain_generator
    shaders/calculate_normal.comp
    shaders/generator.frag
    shaders/decoy.vert
)