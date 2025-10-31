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
  struct CreateInfo
  {
    uint32_t maxNumberOfSamples;
    TerrainGenerationParams params;
  };

public:
  TerrainGeneratorModule();
  explicit TerrainGeneratorModule(CreateInfo info);

  void allocateResources(
    vk::Format map_format = vk::Format::eR32Sfloat, vk::Extent3D extent = {4096, 4096, 1});
  void loadShaders();
  void setupPipelines();
  void execute();

  void drawGui();

  std::vector<etna::Binding> getBindings(vk::ImageLayout layout) const;

  // no flush
  void setMapsState(
    vk::CommandBuffer com_buffer,
    vk::PipelineStageFlags2 pipeline_stage_flags,
    vk::AccessFlags2 access_flags,
    vk::ImageLayout layout,
    vk::ImageAspectFlags aspect_flags);

  const etna::Sampler& getSampler() const { return terrainSampler; }

private:
  etna::Image terrainMap;
  etna::Image terrainNormalMap;

  etna::Sampler terrainSampler;

  TerrainGenerationParams params;
  etna::Buffer paramsBuffer;

  uint32_t maxNumberOfSamples;

  etna::ComputePipeline terrainGenerationPipeline;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};
