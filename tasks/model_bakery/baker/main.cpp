#include "etna/Assert.hpp"

#include <string>

int main(int argc, char* argv[])
{
  if (argc != 1) {
    ETNA_PANIC("Arguments amount is not correct! Expected 1 argument - path to .gltf file.");
  }

  auto path = std::string(argv[1]);
  return 0;
}
