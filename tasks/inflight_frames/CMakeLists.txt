
add_executable(inflight_frames
  main.cpp
  App.cpp
)

add_compile_definitions(inflight_frames
      PRIVATE $<UPPER_CASE:inflight_frames>_TEXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../resources/textures/") # bad but idk how to do better

target_link_libraries(inflight_frames
  PRIVATE glfw etna glm::glm wsi gui render_utils scene)

target_add_shaders(inflight_frames
  shaders/main_shader.frag
  shaders/texture_gen.frag
  shaders/decoy.vert
)