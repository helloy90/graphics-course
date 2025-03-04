#pragma once

#include <optional>

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/GpuSharedResource.hpp>
#include <glm/glm.hpp>

#include "scene/SceneManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"
#include "shaders/DirectionalLight.h"
#include "shaders/Light.h"
#include "shaders/terrain/UniformParams.h"
#include "shaders/terrain/TerrainGenerationParams.h"
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
  void setupTerrainGeneration(vk::Format texture_format, vk::Extent3D extent);
  void generateTerrain();
  void displaceLights();
  void loadLights();
  void loadCubemap();

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image); // vk::ImageView target_image_view);

private:
  void renderScene(
    vk::CommandBuffer cmd_buf,
    etna::Buffer& constants,
    vk::PipelineLayout pipeline_layout,
    etna::Buffer& instance_buffer);

  void renderTerrain(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

  void renderCubemap(vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

  void deferredShading(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

  bool isVisible(const Bounds& bounds, const glm::mat4& proj_view, const glm::mat4& transform);

  void parseInstanceInfo(etna::Buffer& buffer);

  void updateConstants(etna::Buffer& constants);

  void tonemappingShaderStart(
    vk::CommandBuffer cmd_buf,
    const etna::ComputePipeline& current_pipeline,
    std::string shader_program,
    std::vector<etna::Binding> bindings,
    std::optional<uint32_t> push_constant,
    glm::uvec2 group_count);

private:
  std::unique_ptr<SceneManager> sceneMgr;

  vk::Format renderTargetFormat;

  etna::Image mainViewDepth;

  etna::Image terrainMap;
  etna::Image terrainNormalMap;
  std::optional<etna::GpuSharedResource<etna::Buffer>> generationParamsBuffer;
  TerrainGenerationParams generationParams;
  uint32_t maxNumberOfSamples;

  etna::Image cubemapTexture;

  etna::Image renderTarget;

  std::optional<GBuffer> gBuffer;

  std::vector<Light> lights;
  std::vector<DirectionalLight> directionalLights;
  etna::Buffer lightsBuffer;
  etna::Buffer directionalLightsBuffer;

  UniformParams params;

  std::size_t maxInstancesInScene;
  std::optional<etna::GpuSharedResource<etna::Buffer>> instanceMatricesBuffer;
  std::optional<etna::GpuSharedResource<etna::Buffer>> constantsBuffer;
  std::vector<uint32_t> instancesAmount;

  etna::GraphicsPipeline staticMeshPipeline{};
  etna::GraphicsPipeline terrainGenerationPipeline;
  etna::GraphicsPipeline terrainRenderPipeline;
  etna::GraphicsPipeline deferredShadingPipeline;
  etna::GraphicsPipeline cubemapRenderPipeline;

  std::optional<etna::GpuSharedResource<etna::Buffer>> histogramBuffer;
  std::optional<etna::GpuSharedResource<etna::Buffer>> histogramInfoBuffer;
  std::optional<etna::GpuSharedResource<etna::Buffer>> distributionBuffer;

  std::uint32_t binsAmount;

  etna::ComputePipeline terrainNormalPipeline;
  etna::ComputePipeline lightDisplacementPipeline;

  etna::ComputePipeline calculateMinMaxPipeline;
  etna::ComputePipeline histogramPipeline;
  etna::ComputePipeline processHistogramPipeline;
  etna::ComputePipeline distributionPipeline;
  etna::ComputePipeline postprocessComputePipeline;

  etna::Sampler terrainSampler;
  etna::Sampler staticMeshSampler;

  bool wireframeEnabled;
  bool tonemappingEnabled;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;

  glm::uvec2 resolution;
};
