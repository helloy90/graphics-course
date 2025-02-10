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
#include "shaders/UniformParams.h"

class WorldRenderer
{
public:
  WorldRenderer();

  void loadScene(std::filesystem::path path);

  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupRenderPipelines();
  void setupTerrainGeneration(vk::Format texture_format, vk::Extent3D extent);
  void generateTerrain();

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image); // vk::ImageView target_image_view);

private:
  void renderScene(
    vk::CommandBuffer cmd_buf,
    const glm::mat4x4& glob_tm,
    vk::PipelineLayout pipeline_layout,
    etna::Buffer& instance_buffer);

  void renderTerrain(
    vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout);

  bool isVisible(const Bounds& bounds, const glm::mat4& proj_view, const glm::mat4& transform);

  void parseInstanceInfo(etna::Buffer& buffer, const glm::mat4x4& glob_tm);

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

  etna::Image renderTarget;

  UniformParams params;

  std::size_t maxInstancesInScene;
  std::optional<etna::GpuSharedResource<etna::Buffer>> instanceMatricesBuffer;
  std::optional<etna::GpuSharedResource<etna::Buffer>> constantsBuffer;
  std::vector<uint32_t> instancesAmount;

  glm::mat4x4 worldViewProj;

  etna::GraphicsPipeline staticMeshPipeline{};
  etna::GraphicsPipeline terrainGenerationPipeline;
  etna::GraphicsPipeline terrainRenderPipeline;

  std::optional<etna::GpuSharedResource<etna::Buffer>> histogramBuffer;
  std::optional<etna::GpuSharedResource<etna::Buffer>> histogramInfoBuffer;
  std::optional<etna::GpuSharedResource<etna::Buffer>> distributionBuffer;

  std::uint32_t binsAmount;

  etna::ComputePipeline calculateMinMaxPipeline;
  etna::ComputePipeline histogramPipeline;
  etna::ComputePipeline processHistogramPipeline;
  etna::ComputePipeline distributionPipeline;
  etna::ComputePipeline postprocessComputePipeline;

  etna::Sampler terrainSampler;

  bool wireframeEnabled;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;

  glm::uvec2 resolution;
};
