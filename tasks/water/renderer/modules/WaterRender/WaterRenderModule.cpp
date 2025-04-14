#include "WaterRenderModule.hpp"
#include "cpp_glsl_compat.h"

#include <tracy/Tracy.hpp>

#include <imgui.h>

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>


WaterRenderModule::WaterRenderModule()
  : params(
      {.extent = shader_uvec2(1024),
       .chunk = shader_uvec2(16),
       .waterInChunks = shader_uvec2(128),
       .waterOffset = shader_vec2(-1024),
       .extrusionInChunks = shader_uvec2(0),
       .heightOffset = 0.3})
  , renderParams(
      {.color = glm::vec4(0.4627450980, 0.7137254902, 0.7686274510, 1),
       .tipColor = glm::vec4(0.8705882353, 0.9529411765, 0.9647058824, 1),
       .tipAttenuation = 1,
       .roughness = 0.3})
{
}

WaterRenderModule::WaterRenderModule(WaterParams par)
  : params(par)
  , renderParams(
      {.color = glm::vec4(0.4627450980, 0.7137254902, 0.7686274510, 1),
       .tipColor = glm::vec4(0.8705882353, 0.9529411765, 0.9647058824, 1),
       .tipAttenuation = 1,
       .roughness = 0.3})
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

  renderParamsBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(WaterRenderParams),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO,
    .allocationCreate =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .name = "waterRenderParams"});

  paramsBuffer.map();
  std::memcpy(paramsBuffer.data(), &params, sizeof(WaterParams));
  paramsBuffer.unmap();

  renderParamsBuffer.map();
  std::memcpy(renderParamsBuffer.data(), &renderParams, sizeof(WaterRenderParams));
  renderParamsBuffer.unmap();
}

void WaterRenderModule::loadShaders()
{
  etna::create_program(
    "water_render",
    {WATER_RENDER_MODULE_SHADERS_ROOT "chunk.vert.spv",
     WATER_RENDER_MODULE_SHADERS_ROOT "subdivide_chunk.tesc.spv",
     WATER_RENDER_MODULE_SHADERS_ROOT "process_chunk.tese.spv",
     WATER_RENDER_MODULE_SHADERS_ROOT "water.frag.spv"});
}

void WaterRenderModule::setupPipelines(bool wireframe_enabled, vk::Format render_target_format)
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  waterRenderPipeline = pipelineManager.createGraphicsPipeline(
    "water_render",
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
  {
    ETNA_PROFILE_GPU(cmd_buf, renderWater);
    etna::RenderTargetState renderTargets(
      cmd_buf, {{0, 0}, {extent.x, extent.y}}, color_attachment_params, depth_attachment_params);

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, waterRenderPipeline.getVkPipeline());
    renderWater(
      cmd_buf,
      waterRenderPipeline.getVkPipelineLayout(),
      packet,
      water_map,
      water_normal_map,
      water_sampler);
  }
}

void WaterRenderModule::drawGui()
{
  ImGui::Begin("Application Settings");

  static bool renderParamsChanged = false;

  if (ImGui::CollapsingHeader("Water Render"))
  {
    ImGui::SeparatorText("Render parameters");

    ImGuiColorEditFlags colorFlags =
      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoAlpha;

    float color[] = {
      renderParams.color.r,
      renderParams.color.g,
      renderParams.color.b,
    };
    float tipColor[] = {
      renderParams.tipColor.r,
      renderParams.tipColor.g,
      renderParams.tipColor.b,
    };
    float tipAttenuation = renderParams.tipAttenuation;
    float roughness = renderParams.roughness;

    renderParamsChanged =
      renderParamsChanged || ImGui::ColorEdit3("Water Color", color, colorFlags);
    renderParams.color = shader_vec4(color[0], color[1], color[2], 1);
    renderParamsChanged =
      renderParamsChanged || ImGui::ColorEdit3("Water Tip Color", tipColor, colorFlags);
    renderParams.tipColor = shader_vec4(tipColor[0], tipColor[1], tipColor[2], 1);
    renderParamsChanged = renderParamsChanged ||
      ImGui::DragFloat("Water Tip Attenuation", &tipAttenuation, 0.01, 0.0, 500);
    renderParams.tipAttenuation = tipAttenuation;
    renderParamsChanged =
      renderParamsChanged || ImGui::DragFloat("Water Roughness", &roughness, 0.001, 0.0, 1);
    renderParams.roughness = roughness;
  }

  if (renderParamsChanged)
  {
    renderParamsBuffer.map();
    std::memcpy(renderParamsBuffer.data(), &renderParams, sizeof(WaterRenderParams));
    renderParamsBuffer.unmap();
    renderParamsChanged = false;
  }

  ImGui::End();
}

void WaterRenderModule::renderWater(
  vk::CommandBuffer cmd_buf,
  vk::PipelineLayout pipeline_layout,
  const RenderPacket& packet,
  const etna::Image& water_map,
  const etna::Image& water_normal_map,
  const etna::Sampler& water_sampler)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("water_render");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, paramsBuffer.genBinding()},
     etna::Binding{1, renderParamsBuffer.genBinding()},
     etna::Binding{
       2, water_map.genBinding(water_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
     etna::Binding{
       3,
       water_normal_map.genBinding(water_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants<RenderPacket>(
    pipeline_layout,
    vk::ShaderStageFlagBits::eTessellationControl |
      vk::ShaderStageFlagBits::eTessellationEvaluation,
    0,
    {packet});

  cmd_buf.draw(
    4,
    params.waterInChunks.x * params.waterInChunks.y -
      (params.extrusionInChunks.x * params.extrusionInChunks.y),
    0,
    0);
}
