set_source_files_properties(mikktspace.c PROPERTIES LANGUAGE CXX)

add_compile_options(-Wno-pedantic -Wno-unused) # for mikktspace

add_executable(baker
  main.cpp
  Baker.cpp
  mikktspace.c
)

target_link_libraries(baker
  PRIVATE tinygltf glm::glm etna)
