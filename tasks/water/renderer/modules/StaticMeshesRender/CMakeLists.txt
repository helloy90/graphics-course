
add_library(static_meshes MeshesRenderModule.cpp)

target_include_directories(static_meshes PUBLIC ..)

target_include_directories(static_meshes INTERFACE shaders)
# Allow GLSL code to include helper files and compat
target_shader_include_directories(static_meshes INTERFACE shaders)

target_link_libraries(static_meshes PUBLIC etna render_utils gui scene)


target_add_shaders(static_meshes
    shaders/static_mesh.frag
    shaders/static_mesh.vert
    shaders/culling.comp
)