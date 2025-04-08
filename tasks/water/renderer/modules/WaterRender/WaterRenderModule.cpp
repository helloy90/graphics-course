#include "WaterRenderModule.hpp"

#include <tracy/Tracy.hpp>

#include <imgui.h>

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>


WaterRenderModule::WaterRenderModule()
  : params(
      {.extent = shader_uvec2(512),
       .chunk = shader_uvec2(16),
       .waterInChunks = shader_uvec2(64),
       .waterOffset = shader_vec2(-256)})
{
}

WaterRenderModule::WaterRenderModule(WaterParams par)
  : params(par)
{
}

void WaterRenderModule::allocateResources()
{
  paramsBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(WaterParams),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO,
    .allocationCreate =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .name = "waterParams"});


  paramsBuffer.map();
  std::memcpy(paramsBuffer.data(), &params, sizeof(WaterParams));
  paramsBuffer.unmap();
}

void WaterRenderModule::loadShaders()
{
  etna::create_program(
    "terrain_render",
    {TERRAIN_RENDER_MODULE_SHADERS_ROOT "chunk.vert.spv",
     TERRAIN_RENDER_MODULE_SHADERS_ROOT "subdivide_chunk.tesc.spv",
     TERRAIN_RENDER_MODULE_SHADERS_ROOT "process_chunk.tese.spv",
     TERRAIN_RENDER_MODULE_SHADERS_ROOT "terrain.frag.spv"});
}

void WaterRenderModule::setupPipelines(bool wireframe_enabled, vk::Format render_target_format) {}

void WaterRenderModule::execute(
  vk::CommandBuffer cmd_buf,
  const RenderPacket& packet,
  glm::uvec2 extent,
  std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
  etna::RenderTargetState::AttachmentParams depth_attachment_params,
  const etna::Image& water_map,
  const etna::Image& water_normal_map,
  const etna::Sampler& water_sampler)
{
}
