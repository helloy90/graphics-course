#pragma once

#include <glm/fwd.hpp>
#include <glm/glm.hpp>

#include <etna/GraphicsPipeline.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>
#include <etna/OneShotCmdMgr.hpp>

#include "shaders/TerrainParams.h"
#include "../RenderPacket.hpp"


class TerrainRenderModule
{
public:
  TerrainRenderModule();
  explicit TerrainRenderModule(TerrainParams par);

  void allocateResources();
  void loadShaders();
  void setupPipelines(bool wireframe_enabled, vk::Format render_target_format);

  void loadMaps(const std::vector<etna::Binding>& terrain_bindings);

  void executeRender(
    vk::CommandBuffer cmd_buf,
    const RenderPacket& packet,
    std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
    etna::RenderTargetState::AttachmentParams depth_attachment_params);

  void executeShadowMapping(
    vk::CommandBuffer cmd_buf,
    const RenderPacket& packet,
    vk::Extent2D extent,
    const etna::Buffer& light_info,
    etna::RenderTargetState::AttachmentParams shadow_mapping_attachment_params);

  void drawGui();

private:
  struct PushConstants
  {
    glm::mat4x4 projView;
    glm::vec3 cameraWorldPosition;
  };

private:
  void renderTerrain(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, const RenderPacket& packet);

private:
  TerrainParams params;

  etna::Buffer paramsBuffer;

  std::unique_ptr<etna::PersistentDescriptorSet> terrainSet;
  std::unique_ptr<etna::PersistentDescriptorSet> terrainShadowSet;

  etna::GraphicsPipeline terrainRenderPipeline;
  etna::GraphicsPipeline terrainShadowPipeline;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};
