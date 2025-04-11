#pragma once

#include <memory>
#include <optional>

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/GpuSharedResource.hpp>
#include <glm/glm.hpp>

#include "wsi/Keyboard.hpp"

#include "modules/Light/LightModule.hpp"
#include "modules/StaticMeshesRender/MeshesRenderModule.hpp"
#include "modules/TerrainGenerator/TerrainGeneratorModule.hpp"
#include "modules/TerrainRender/TerrainRenderModule.hpp"
#include "modules/Tonemapping/TonemappingModule.hpp"
#include "modules/WaterGenerator/WaterGeneratorModule.hpp"
#include "modules/WaterRender/WaterRenderModule.hpp"

#include "modules/RenderPacket.hpp"

#include "FramePacket.hpp"

#include "shaders/UniformParams.h"
#include "GBuffer.hpp"


class WorldRenderer
{
public:
  WorldRenderer();

  void loadScene(std::filesystem::path path);
  void allocateResources(glm::uvec2 swapchain_resolution);
  void loadShaders();
  void setupRenderPipelines();
  void rebuildRenderPipelines();

  void loadCubemap();

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(vk::CommandBuffer cmd_buf, vk::Image target_image);

private:
  void deferredShading(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

private:
  LightModule lightModule;
  MeshesRenderModule staticMeshesRenderModule;
  TerrainGeneratorModule terrainGeneratorModule;
  TerrainRenderModule terrainRenderModule;
  TonemappingModule tonemappingModule;
  WaterGeneratorModule waterGeneratorModule;
  WaterRenderModule waterRenderModule;

  vk::Format renderTargetFormat;

  etna::Image cubemapTexture;

  etna::Image renderTarget;

  std::optional<GBuffer> gBuffer;

  UniformParams params;
  RenderPacket renderPacket;

  std::optional<etna::GpuSharedResource<etna::Buffer>> constantsBuffer;

  etna::GraphicsPipeline deferredShadingPipeline;

  bool wireframeEnabled;
  bool tonemappingEnabled;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;

  glm::uvec2 resolution;
};
