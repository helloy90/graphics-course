
add_executable(terrain_baker
  main.cpp
  Baker.cpp
)

target_link_libraries(terrain_baker
  PRIVATE tinygltf glm::glm etna)
