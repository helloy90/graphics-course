
add_executable(local_shadertoy2
  main.cpp
  App.cpp
)

add_compile_definitions(local_shadertoy2
      PRIVATE $<UPPER_CASE:local_shadertoy2>_TEXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../resources/textures/") # bad but idk how to do better

target_link_libraries(local_shadertoy2
  PRIVATE glfw etna glm::glm wsi gui render_utils scene)

target_add_shaders(local_shadertoy2
  shaders/main_shader.frag
  shaders/texture_gen.frag
  shaders/decoy.vert
)