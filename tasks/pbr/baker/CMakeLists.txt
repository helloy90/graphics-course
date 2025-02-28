add_executable(baker
  main.cpp
  Baker.cpp
)

target_link_libraries(baker
  PRIVATE tinygltf glm::glm etna)
