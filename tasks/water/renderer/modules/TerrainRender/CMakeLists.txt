
add_library(terrain_render TerrainRenderModule.cpp)

target_include_directories(terrain_render PUBLIC ..)

target_include_directories(terrain_render INTERFACE shaders)
# Allow GLSL code to include helper files and compat
target_shader_include_directories(terrain_render INTERFACE shaders)

target_link_libraries(terrain_render PUBLIC etna render_utils gui)


target_add_shaders(terrain_render
    shaders/chunk.vert
    shaders/subdivide_chunk.tesc
    shaders/process_chunk.tese
    shaders/terrain.frag
)