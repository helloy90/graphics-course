#pragma once

#include <etna/GraphicsPipeline.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>

#include "shaders/WaterParams.h"
#include "shaders/WaterRenderParams.h"
#include "../RenderPacket.hpp"


class WaterRenderModule
{
public:
  WaterRenderModule();
  explicit WaterRenderModule(WaterParams par);

  void allocateResources();
  void loadShaders();
  void setupPipelines(bool wireframe_enabled, vk::Format render_target_format);
  // TODO - make persistent desc set for shadows
  void executeRender(
    vk::CommandBuffer cmd_buf,
    const RenderPacket& packet,
    std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
    etna::RenderTargetState::AttachmentParams depth_attachment_params,
    const etna::Image& water_map,
    const etna::Image& water_normal_map,
    const std::vector<etna::Binding>& shadow,
    const etna::Sampler& water_sampler,
    const etna::Buffer& directional_lights_buffer,
    const etna::Image& cubemap);

  void drawGui();

private:
  struct PushConstants
  {
    glm::mat4x4 projView;
    glm::vec3 cameraWorldPosition;
  };

private:
  void renderWater(
    vk::CommandBuffer cmd_buf,
    vk::PipelineLayout pipeline_layout,
    const RenderPacket& packet,
    const etna::Image& water_map,
    const etna::Image& water_normal_map,
    const std::vector<etna::Binding>& shadow,
    const etna::Sampler& water_sampler,
    const etna::Buffer& directional_lights_buffer,
    const etna::Image& cubemap);

private:
  WaterParams params;
  etna::Buffer paramsBuffer;

  WaterRenderParams renderParams;
  etna::Buffer renderParamsBuffer;

  etna::GraphicsPipeline waterRenderPipeline;
};
