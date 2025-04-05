
add_executable(project_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  GBuffer.cpp
)

add_subdirectory(modules)

target_link_libraries(project_renderer
  PRIVATE glfw etna glm::glm wsi gui scene render_utils 
  terrain_generator tonemapping static_meshes terrain_render)

target_add_shaders(project_renderer
  shaders/decoy.vert
  shaders/shading.frag
)
