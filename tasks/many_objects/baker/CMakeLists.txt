
add_executable(many_objects_baker
  main.cpp
  Baker.cpp
)

target_link_libraries(many_objects_baker
  PRIVATE tinygltf glm::glm etna)
