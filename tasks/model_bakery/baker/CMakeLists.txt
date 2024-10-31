
add_executable(model_bakery_baker
  main.cpp
  Baker.cpp
)

target_link_libraries(model_bakery_baker
  PRIVATE tinygltf etna)
