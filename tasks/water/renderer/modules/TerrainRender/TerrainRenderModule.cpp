#include "TerrainRenderModule.hpp"

#include <tracy/Tracy.hpp>

#include <imgui.h>

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>

#include "HeightParams.hpp"
#include "shaders/TerrainParams.h"


TerrainRenderModule::TerrainRenderModule()
  : params({
      .extent = shader_uvec2(4096),
      .chunk = shader_uvec2(16),
      .terrainInChunks = shader_uvec2(64, 64),
      .terrainOffset = shader_vec2(-512, -512),

    })
  , heightParams({.amplifier = shader_float(200.0f), .offset = shader_float(0.6f)})
{
}

TerrainRenderModule::TerrainRenderModule(TerrainParams par)
  : params(par)
  , heightParams({.amplifier = shader_float(200.0f), .offset = shader_float(0.6f)})
{
}

TerrainRenderModule::TerrainRenderModule(HeightParams par)
  : params(
      {.extent = shader_uvec2(4096),
       .chunk = shader_uvec2(16, 16),
       .terrainInChunks = shader_uvec2(64, 64),
       .terrainOffset = shader_vec2(-512, -512)})
  , heightParams(par)
{
}

void TerrainRenderModule::allocateResources()
{
  paramsBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(TerrainParams),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO,
    .allocationCreate =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .name = "terrainParams"});

  heightParamsBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(HeightParams),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO,
    .allocationCreate =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .name = "terrainHeightParams"});

  paramsBuffer.map();
  std::memcpy(paramsBuffer.data(), &params, sizeof(TerrainParams));
  paramsBuffer.unmap();

  heightParamsBuffer.map();
  std::memcpy(heightParamsBuffer.data(), &heightParams, sizeof(HeightParams));
  heightParamsBuffer.unmap();
}

void TerrainRenderModule::loadShaders()
{
  etna::create_program(
    "terrain_render",
    {TERRAIN_RENDER_MODULE_SHADERS_ROOT "chunk.vert.spv",
     TERRAIN_RENDER_MODULE_SHADERS_ROOT "subdivide_chunk.tesc.spv",
     TERRAIN_RENDER_MODULE_SHADERS_ROOT "process_chunk.tese.spv",
     TERRAIN_RENDER_MODULE_SHADERS_ROOT "terrain.frag.spv"});
}

void TerrainRenderModule::setupPipelines(bool wireframe_enabled, vk::Format render_target_format)
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  terrainRenderPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_render",
    etna::GraphicsPipeline::CreateInfo{
      .inputAssemblyConfig = {.topology = vk::PrimitiveTopology::ePatchList},
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = (wireframe_enabled ? vk::PolygonMode::eLine : vk::PolygonMode::eFill),
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .blendingConfig =
        {
          .attachments =
            {{
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             },
             {
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             },
             {
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             }},
          .logicOpEnable = false,
          .logicOp = {},
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats =
            {render_target_format, vk::Format::eR8G8B8A8Snorm, vk::Format::eR8G8B8A8Unorm},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });
}

void TerrainRenderModule::execute(
  vk::CommandBuffer cmd_buf,
  const RenderPacket& packet,
  glm::uvec2 extent,
  std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
  etna::RenderTargetState::AttachmentParams depth_attachment_params,
  const etna::Image& terrain_map,
  const etna::Image& terrain_normal_map,
  const etna::Sampler& terrain_sampler)
{
  {
    ETNA_PROFILE_GPU(cmd_buf, renderTerrain);
    etna::RenderTargetState renderTargets(
      cmd_buf, {{0, 0}, {extent.x, extent.y}}, color_attachment_params, depth_attachment_params);

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainRenderPipeline.getVkPipeline());
    renderTerrain(
      cmd_buf,
      terrainRenderPipeline.getVkPipelineLayout(),
      packet,
      terrain_map,
      terrain_normal_map,
      terrain_sampler);
  }
}

void TerrainRenderModule::drawGui()
{
  ImGui::Begin("Application Settings");

  if (ImGui::CollapsingHeader("Terrain Render"))
  {
    ImGui::SeparatorText("Height Adjustment");
    ImGui::SliderFloat("Height Amplifier", &heightParams.amplifier, 0, 10000, "%.3f");
    ImGui::SliderFloat("Height Offset", &heightParams.offset, -1.0f, 1.0f, "%.5f");
    if (ImGui::Button("Apply"))
    {
      heightParamsBuffer.map();
      std::memcpy(heightParamsBuffer.data(), &heightParams, sizeof(HeightParams));
      heightParamsBuffer.unmap();
    }
  }

  ImGui::End();
}

void TerrainRenderModule::renderTerrain(
  vk::CommandBuffer cmd_buf,
  vk::PipelineLayout pipeline_layout,
  const RenderPacket& packet,
  const etna::Image& terrain_map,
  const etna::Image& terrain_normal_map,
  const etna::Sampler& terrain_sampler)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("terrain_render");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, paramsBuffer.genBinding()},
     etna::Binding{1, heightParamsBuffer.genBinding()},
     etna::Binding{
       2, terrain_map.genBinding(terrain_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
     etna::Binding{
       3,
       terrain_normal_map.genBinding(
         terrain_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants<RenderPacket>(
    pipeline_layout,
    vk::ShaderStageFlagBits::eTessellationControl |
      vk::ShaderStageFlagBits::eTessellationEvaluation,
    0,
    {packet});

  cmd_buf.draw(4, params.terrainInChunks.x * params.terrainInChunks.y, 0, 0);
}
