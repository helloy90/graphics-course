#pragma once

#include <etna/GraphicsPipeline.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>

#include "shaders/WaterParams.h"
#include "../RenderPacket.hpp"

class WaterRenderModule
{
public:
  WaterRenderModule();
  explicit WaterRenderModule(WaterParams par);

  void allocateResources();
  void loadShaders();
  void setupPipelines(bool wireframe_enabled, vk::Format render_target_format);
  void execute(
    vk::CommandBuffer cmd_buf,
    const RenderPacket& packet,
    glm::uvec2 extent,
    std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
    etna::RenderTargetState::AttachmentParams depth_attachment_params,
    const etna::Image& water_map,
    const etna::Image& water_normal_map,
    const etna::Sampler& water_sampler);

private:
  WaterParams params;

  etna::Buffer paramsBuffer;

  etna::GraphicsPipeline waterRenderPipeline;
};
