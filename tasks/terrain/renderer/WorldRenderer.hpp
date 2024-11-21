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
  void setupMeshPipelines(vk::Format swapchain_format);
  void setupTerrainResources(
    vk::Format swapchain_format, vk::Format texture_format, vk::Extent3D extent);
  void generateTerrain();

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

  void renderTerrain(
    vk::CommandBuffer cmd_buf, const glm::mat4x4& glob_tm, vk::PipelineLayout pipeline_layout);

  bool isVisible(const Bounds& bounds, const glm::mat4& proj_view, const glm::mat4& transform);

  void parseInstanceInfo(etna::Buffer& current_buffer, const glm::mat4x4& glob_tm);

private:
  std::unique_ptr<SceneManager> sceneMgr;

  etna::Image mainViewDepth;
  etna::Image terrainMap;

  struct TesselatorPushConstants
  {
    glm::mat4x4 projView;
    glm::vec3 cameraPosition;
  };

  std::size_t maxInstancesInScene;
  std::optional<etna::GpuSharedResource<etna::Buffer>> instanceMatricesBuffer;
  std::vector<uint32_t> instancesAmount;

  glm::mat4x4 worldViewProj;
  glm::mat4x4 lightMatrix;
  glm::vec3 cameraPosition;

  etna::GraphicsPipeline staticMeshPipeline{};
  etna::GraphicsPipeline terrainGenerationPipeline;
  etna::GraphicsPipeline terrainRenderPipeline;

  etna::Sampler terrainSampler;
  std::uint32_t chunksAmount;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;

  glm::uvec2 resolution;
};
