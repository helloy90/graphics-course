
add_executable(deferred_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
)

target_link_libraries(deferred_renderer
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(deferred_renderer
  shaders/static_mesh.frag
  shaders/static_mesh.vert
  shaders/decoy.vert
  shaders/generator.frag
  shaders/chunk.vert
  shaders/subdivide_chunk.tesc
  shaders/process_chunk.tese
  shaders/terrain.frag
  shaders/postprocessing/calculate_min_max.comp
  shaders/postprocessing/histogram.comp
  shaders/postprocessing/process_histogram.comp
  shaders/postprocessing/postprocess.comp
)
