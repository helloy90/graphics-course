
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
  terrain_generator tonemapping static_meshes)

target_add_shaders(project_renderer
  shaders/decoy.vert
  shaders/terrain/displace_lights.comp
  shaders/terrain/chunk.vert
  shaders/terrain/subdivide_chunk.tesc
  shaders/terrain/process_chunk.tese
  shaders/terrain/terrain.frag
  shaders/shading.frag
)
