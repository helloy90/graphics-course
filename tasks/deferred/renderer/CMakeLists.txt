
add_executable(deferred_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  GBuffer.cpp
)

target_link_libraries(deferred_renderer
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(deferred_renderer
  shaders/meshes/static_mesh.frag
  shaders/meshes/static_mesh.vert
  shaders/decoy.vert
  shaders/terrain/calculate_normal.comp
  shaders/terrain/displace_lights.comp
  shaders/terrain/generator.frag
  shaders/terrain/chunk.vert
  shaders/terrain/subdivide_chunk.tesc
  shaders/terrain/process_chunk.tese
  shaders/terrain/terrain.frag
  shaders/postprocessing/calculate_min_max.comp
  shaders/postprocessing/histogram.comp
  shaders/postprocessing/process_histogram.comp
  shaders/postprocessing/postprocess.comp
  shaders/shading.frag
)
