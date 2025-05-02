
add_library(shadow_mapping_module ShadowMappingModule.cpp)

target_include_directories(shadow_mapping_module PUBLIC ..)

target_include_directories(shadow_mapping_module INTERFACE shaders)
# Allow GLSL code to include helper files and compat
target_shader_include_directories(shadow_mapping_module INTERFACE shaders)

target_link_libraries(shadow_mapping_module PUBLIC etna render_utils gui)


target_add_shaders(static_meshes_module

)