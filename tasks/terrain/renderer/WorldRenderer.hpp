#pragma once

#include <optional>

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/GpuSharedResource.hpp>
#include <glm/glm.hpp>

#include "scene/SceneManager.hpp"
#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"


class WorldRenderer
{
public:
  WorldRenderer();

  void loadScene(std::filesystem::path path);

  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupPipelines(vk::Format swapchain_format);

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view);

private:
  void renderScene(
    vk::CommandBuffer cmd_buf,
    const glm::mat4x4& glob_tm,
    vk::PipelineLayout pipeline_layout,
    etna::Buffer& current_instance_buffer);

  bool isVisible(const Bounds& bounds, const glm::mat4& proj_view, const glm::mat4& transform);

  void parseInstanceInfo(etna::Buffer& current_buffer, const glm::mat4x4& glob_tm);

private:
  std::unique_ptr<SceneManager> sceneMgr;

  etna::Image mainViewDepth;
  etna::Image heightMap;

  struct PushConstants
  {
    glm::mat4x4 projView;
  } pushConst2M;

  std::size_t maxInstancesInScene;
  std::optional<etna::GpuSharedResource<etna::Buffer>> instanceMatricesBuffer;
  std::vector<uint32_t> instancesAmount;

  glm::mat4x4 worldViewProj;
  glm::mat4x4 lightMatrix;

  etna::GraphicsPipeline staticMeshPipeline{};
  etna::GraphicsPipeline terrainGenerationPipeline;

  glm::uvec2 resolution;
};