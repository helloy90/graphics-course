#pragma once

#include <glm/glm.hpp>

#include <etna/GraphicsPipeline.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>

#include "shaders/TerrainParams.h"
#include "../RenderPacket.hpp"
#include "../HeightParams.hpp"

class TerrainRenderModule
{
public:
  TerrainRenderModule();
  explicit TerrainRenderModule(TerrainParams par);
  explicit TerrainRenderModule(HeightParams par);

  void allocateResources();
  void loadShaders();
  void setupPipelines(bool wireframe_enabled, vk::Format render_target_format);
  void execute(
    vk::CommandBuffer cmd_buf,
    const RenderPacket& packet,
    glm::uvec2 extent,
    std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
    etna::RenderTargetState::AttachmentParams depth_attachment_params,
    const etna::Image& terrain_map,
    const etna::Image& terrain_normal_map,
    const etna::Sampler& terrain_sampler);

  void drawGui();

  const etna::Buffer& getHeightParamsBuffer() const { return heightParamsBuffer; }

private:
  void renderTerrain(
    vk::CommandBuffer cmd_buf,
    vk::PipelineLayout pipeline_layout,
    const RenderPacket& packet,
    const etna::Image& terrain_map,
    const etna::Image& terrain_normal_map,
    const etna::Sampler& terrain_sampler);

private:
  TerrainParams params;
  HeightParams heightParams;

  etna::Buffer paramsBuffer;
  etna::Buffer heightParamsBuffer;

  etna::GraphicsPipeline terrainRenderPipeline;
};
