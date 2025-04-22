set_source_files_properties(mikktspace.c PROPERTIES LANGUAGE CXX)

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU") # gcc and clang
  add_compile_options(-Wno-pedantic -Wno-unused) # for mikktspace
endif()

add_executable(baker
  main.cpp
  Baker.cpp
  mikktspace.c
)

target_link_libraries(baker
  PRIVATE tinygltf glm::glm etna)
