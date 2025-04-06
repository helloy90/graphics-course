#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GpuSharedResource.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/OneShotCmdMgr.hpp>

#include "/shaders/TerrainGenerationParams.h"


class TerrainGeneratorModule
{
public:
  // const ModuleType type = ModuleType::Generator;

  TerrainGeneratorModule();
  TerrainGeneratorModule(
    vk::Format map_format, vk::Extent3D extent, glm::uvec2 res, uint32_t max_number_of_samples);

  void allocateResources();
  void loadShaders();
  void setupPipelines();
  void execute();

  void drawGui();

  const etna::Image& getMap() const { return terrainMap; }
  const etna::Image& getNormalMap() const { return terrainNormalMap; }
  const etna::Sampler& getSampler() const { return terrainSampler; }

private:
  vk::Format mapFormat;
  vk::Extent3D extent;

  glm::uvec2 resolution;

  etna::Image terrainMap;
  etna::Image terrainNormalMap;

  etna::Sampler terrainSampler;

  std::optional<etna::GpuSharedResource<etna::Buffer>> generationParamsBuffer;
  TerrainGenerationParams generationParams;
  uint32_t maxNumberOfSamples;

  etna::GraphicsPipeline terrainGenerationPipeline;
  etna::ComputePipeline terrainNormalPipeline;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};

// static_assert(Module<TerrainGeneratorModule>);
