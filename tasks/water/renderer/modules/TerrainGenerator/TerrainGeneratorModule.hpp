#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GpuSharedResource.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/OneShotCmdMgr.hpp>

#include "shaders/TerrainGenerationParams.h"


class TerrainGeneratorModule
{
public:
  // const ModuleType type = ModuleType::Generator;

  TerrainGeneratorModule();
  explicit TerrainGeneratorModule(uint32_t max_number_of_samples);

  void allocateResources(
    vk::Format map_format = vk::Format::eR32Sfloat, vk::Extent3D extent = {4096, 4096, 1});
  void loadShaders();
  void setupPipelines();
  void execute(glm::vec2 normal_map_fidelity = {16, 16});

  void drawGui();

  const etna::Image& getMap() const { return terrainMap; }
  const etna::Image& getNormalMap() const { return terrainNormalMap; }
  const etna::Sampler& getSampler() const { return terrainSampler; }

private:
  etna::Image terrainMap;
  etna::Image terrainNormalMap;

  etna::Sampler terrainSampler;

  TerrainGenerationParams params;
  etna::Buffer paramsBuffer;

  uint32_t maxNumberOfSamples;

  etna::GraphicsPipeline terrainGenerationPipeline;
  etna::ComputePipeline terrainNormalPipeline;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};

// static_assert(Module<TerrainGeneratorModule>);
