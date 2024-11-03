
add_executable(model_bakery_baker
  main.cpp
  Baker.cpp
)

add_compile_options(-g)

target_link_libraries(model_bakery_baker
  PRIVATE tinygltf etna)
