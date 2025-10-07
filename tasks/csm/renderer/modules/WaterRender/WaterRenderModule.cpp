#include "WaterRenderModule.hpp"
#include "cpp_glsl_compat.h"
#include "etna/DescriptorSet.hpp"

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
       .heightOffset = shader_float(0.3)})
  , renderParams(
      {.scatterColor = shader_vec4(0.016, 0.0736, 0.16, 1),
       .bubbleColor = shader_vec4(0, 0.02, 0.016, 1),
       .foamColor = shader_vec4(0.6, 0.5568, 0.492, 1),
       .roughness = shader_float(0.1),
       .reflectionStrength = shader_float(0.9),
       .wavePeakScatterStrength = shader_float(2.2),
       .scatterStrength = shader_float(1),
       .scatterShadowStrength = shader_float(0.7),
       .bubbleDensity = shader_float(1.3)})
{
}

WaterRenderModule::WaterRenderModule(WaterParams par)
  : params(par)
  , renderParams(
      //{.color = shader_vec4(0.4627450980, 0.7137254902, 0.7686274510, 1),
      {.scatterColor = shader_vec4(0.016, 0.0736, 0.16, 1),
       .bubbleColor = shader_vec4(0, 0.02, 0.016, 1),
       .foamColor = shader_vec4(0.6, 0.5568, 0.0492, 1),
       .roughness = shader_float(0.3),
       .reflectionStrength = shader_float(0.5),
       .wavePeakScatterStrength = shader_float(1),
       .scatterStrength = shader_float(1),
       .scatterShadowStrength = shader_float(0.5),
       .bubbleDensity = shader_float(1)})
{
}

void WaterRenderModule::allocateResources()
{
  paramsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(WaterParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = "waterParams"});

  renderParamsBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
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
            {
              {
                .blendEnable = vk::True,
                .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
                .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
                .colorBlendOp = vk::BlendOp::eAdd,
                .srcAlphaBlendFactor = vk::BlendFactor::eOne,
                .dstAlphaBlendFactor = vk::BlendFactor::eZero,
                .alphaBlendOp = vk::BlendOp::eAdd,
                .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                  vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
              },
            },
          .logicOpEnable = false,
          .logicOp = {},
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {render_target_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });
}

void WaterRenderModule::executeRender(
  vk::CommandBuffer cmd_buf,
  const RenderPacket& packet,
  std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
  etna::RenderTargetState::AttachmentParams depth_attachment_params,
  const etna::Image& water_map,
  const etna::Image& water_normal_map,
  const std::vector<etna::Binding>& shadow,
  const etna::Sampler& water_sampler,
  const etna::Buffer& directional_lights_buffer,
  const etna::Image& cubemap)
{
  {
    ETNA_PROFILE_GPU(cmd_buf, renderWater);
    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {packet.resolution.x, packet.resolution.y}},
      color_attachment_params,
      depth_attachment_params);

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, waterRenderPipeline.getVkPipeline());
    renderWater(
      cmd_buf,
      waterRenderPipeline.getVkPipelineLayout(),
      packet,
      water_map,
      water_normal_map,
      shadow,
      water_sampler,
      directional_lights_buffer,
      cubemap);
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

    float scatterColor[] = {
      renderParams.scatterColor.r,
      renderParams.scatterColor.g,
      renderParams.scatterColor.b,
    };
    float bubbleColor[] = {
      renderParams.bubbleColor.r,
      renderParams.bubbleColor.g,
      renderParams.bubbleColor.b,
    };
    float foamColor[] = {
      renderParams.foamColor.r,
      renderParams.foamColor.g,
      renderParams.foamColor.b,
    };

    float roughness = renderParams.roughness;
    float reflectionStrength = renderParams.reflectionStrength;
    float wavePeakScatterStrength = renderParams.wavePeakScatterStrength;
    float scatterStrength = renderParams.scatterStrength;
    float scatterShadowStrength = renderParams.scatterShadowStrength;
    float bubbleDensity = renderParams.bubbleDensity;

    renderParamsChanged =
      renderParamsChanged || ImGui::ColorEdit3("Water Scatter Color", scatterColor, colorFlags);
    renderParams.scatterColor = shader_vec4(scatterColor[0], scatterColor[1], scatterColor[2], 1);
    renderParamsChanged =
      renderParamsChanged || ImGui::ColorEdit3("Water Bubbles Color", bubbleColor, colorFlags);
    renderParams.bubbleColor = shader_vec4(bubbleColor[0], bubbleColor[1], bubbleColor[2], 1);
    renderParamsChanged =
      renderParamsChanged || ImGui::ColorEdit3("Water Foam Color", foamColor, colorFlags);
    renderParams.foamColor = shader_vec4(foamColor[0], foamColor[1], foamColor[2], 1);
    renderParamsChanged =
      renderParamsChanged || ImGui::DragFloat("Water Roughness", &roughness, 0.001f, 0.0f, 1.0f);
    renderParams.roughness = roughness;
    renderParamsChanged = renderParamsChanged ||
      ImGui::DragFloat("Water Reflection Strength", &reflectionStrength, 0.1f, 0.0f, 500.0f);
    renderParams.reflectionStrength = reflectionStrength;
    renderParamsChanged =
      renderParamsChanged ||
      ImGui::DragFloat(
        "Water Wave Peak Scatter Strength", &wavePeakScatterStrength, 0.1f, 0.0f, 500.0f);
    renderParams.wavePeakScatterStrength = wavePeakScatterStrength;
    renderParamsChanged = renderParamsChanged ||
      ImGui::DragFloat("Water Scatter Strength", &scatterStrength, 0.1f, 0.0f, 500.0f);
    renderParams.scatterStrength = scatterStrength;
    renderParamsChanged = renderParamsChanged ||
      ImGui::DragFloat("Water Scatter Shadow Strength", &scatterShadowStrength, 0.1f, 0.0f, 500.0f);
    renderParams.scatterShadowStrength = scatterShadowStrength;
    renderParamsChanged = renderParamsChanged ||
      ImGui::DragFloat("Water Bubbles Density", &bubbleDensity, 0.1f, 0.0f, 500.0f);
    renderParams.bubbleDensity = bubbleDensity;
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
  const std::vector<etna::Binding>& shadow,
  const etna::Sampler& water_sampler,
  const etna::Buffer& directional_lights_buffer,
  const etna::Image& cubemap)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("water_render");

  std::vector<etna::Binding> bindings;
  bindings.reserve(6 + shadow.size());

  bindings.emplace_back(etna::Binding{0, paramsBuffer.genBinding()});
  bindings.emplace_back(etna::Binding{1, renderParamsBuffer.genBinding()});
  bindings.emplace_back(
    etna::Binding{
      2, water_map.genBinding(water_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)});
  bindings.emplace_back(
    etna::Binding{
      3,
      water_normal_map.genBinding(water_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)});
  for (std::size_t i = 0; i < shadow.size(); i++)
  {
    bindings.emplace_back(std::move(shadow[i]));
  }
  bindings.emplace_back(
    etna::Binding{
      5,
      cubemap.genBinding(
        water_sampler.get(),
        vk::ImageLayout::eShaderReadOnlyOptimal,
        {.type = vk::ImageViewType::eCube})});
  bindings.emplace_back(etna::Binding{6, directional_lights_buffer.genBinding()});


  auto set = etna::create_descriptor_set(shaderInfo.getDescriptorLayoutId(0), cmd_buf, bindings);

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants<PushConstants>(
    pipeline_layout,
    vk::ShaderStageFlagBits::eTessellationControl |
      vk::ShaderStageFlagBits::eTessellationEvaluation | vk::ShaderStageFlagBits::eFragment,
    0,
    {{packet.projView, packet.cameraWorldPosition}});

  cmd_buf.draw(
    4,
    params.waterInChunks.x * params.waterInChunks.y -
      (params.extrusionInChunks.x * params.extrusionInChunks.y),
    0,
    0);
}
