
add_library(tonemapping TonemappingModule.cpp)

target_include_directories(tonemapping PUBLIC ..)

target_include_directories(tonemapping INTERFACE shaders)
# Allow GLSL code to include helper files and compat
target_shader_include_directories(tonemapping INTERFACE shaders)

target_link_libraries(tonemapping PUBLIC etna render_utils gui)


target_add_shaders(tonemapping
    shaders/calculate_min_max.comp
    shaders/histogram.comp
    shaders/process_histogram.comp
    shaders/postprocess.comp
)