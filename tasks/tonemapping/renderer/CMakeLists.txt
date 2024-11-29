
add_executable(tonemapping_renderer
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
)

target_link_libraries(tonemapping_renderer
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(tonemapping_renderer
  shaders/static_mesh.frag
  shaders/static_mesh.vert
  shaders/decoy.vert
  shaders/generator.frag
  shaders/chunk.vert
  shaders/subdivide_chunk.tesc
  shaders/process_chunk.tese
  shaders/terrain.frag
  shaders/postprocessing/histogram.comp
  shaders/postprocessing/process_histogram.comp
  shaders/postprocessing/postprocess.frag
)
