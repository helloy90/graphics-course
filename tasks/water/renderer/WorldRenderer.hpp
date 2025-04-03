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

#include "modules/TerrainGenerator/TerrainGeneratorModule.hpp"
#include "scene/SceneManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"
#include "shaders/DirectionalLight.h"
#include "shaders/Light.h"
#include "shaders/terrain/UniformParams.h"
#include "GBuffer.hpp"


class WorldRenderer
{
public:
  WorldRenderer();

  void loadScene(std::filesystem::path path);
  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupRenderPipelines();
  void rebuildRenderPipelines();
  void loadTerrain();
  void displaceLights();
  void loadLights();
  void loadCubemap();

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image);

private:
  void cullMeshes(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

  void renderScene(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

  void renderTerrain(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

  void deferredShading(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

  void updateConstants(etna::Buffer& constants);

  void tonemappingShaderStart(
    vk::CommandBuffer cmd_buf,
    const etna::ComputePipeline& current_pipeline,
    std::string shader_program,
    std::vector<etna::Binding> bindings,
    std::optional<uint32_t> push_constant,
    glm::uvec2 group_count);

private:
  TerrainGeneratorModule terrainGenerator;

  std::unique_ptr<SceneManager> sceneMgr;

  vk::Format renderTargetFormat;

  etna::Image mainViewDepth;

  etna::Image cubemapTexture;

  etna::Image renderTarget;

  std::optional<GBuffer> gBuffer;

  std::vector<Light> lights;
  std::vector<DirectionalLight> directionalLights;
  etna::Buffer lightsBuffer;
  etna::Buffer directionalLightsBuffer;

  std::optional<etna::PersistentDescriptorSet> meshesDescriptorSet;

  UniformParams params;

  std::optional<etna::GpuSharedResource<etna::Buffer>> constantsBuffer;

  std::optional<etna::GpuSharedResource<etna::Buffer>> histogramBuffer;
  std::optional<etna::GpuSharedResource<etna::Buffer>> histogramInfoBuffer;
  std::optional<etna::GpuSharedResource<etna::Buffer>> distributionBuffer;

  std::uint32_t binsAmount;

  etna::GraphicsPipeline staticMeshPipeline{};
  etna::GraphicsPipeline terrainRenderPipeline;
  etna::GraphicsPipeline deferredShadingPipeline;

  etna::ComputePipeline cullingPipeline;

  etna::ComputePipeline lightDisplacementPipeline;

  etna::ComputePipeline calculateMinMaxPipeline;
  etna::ComputePipeline histogramPipeline;
  etna::ComputePipeline processHistogramPipeline;
  etna::ComputePipeline distributionPipeline;
  etna::ComputePipeline postprocessComputePipeline;

  etna::Sampler staticMeshSampler;

  bool wireframeEnabled;
  bool tonemappingEnabled;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;

  glm::uvec2 resolution;
};
