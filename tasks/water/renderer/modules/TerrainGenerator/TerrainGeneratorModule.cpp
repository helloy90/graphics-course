#include "TerrainGeneratorModule.hpp"

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>

#include "imgui.h"


TerrainGeneratorModule::TerrainGeneratorModule()
  : mapFormat(vk::Format::eR32Sfloat)
  , extent({4096, 4096, 1})
  , resolution({16, 16})
  , maxNumberOfSamples(16)
{
}

TerrainGeneratorModule::TerrainGeneratorModule(
  vk::Format map_format, vk::Extent3D extent, glm::uvec2 res, uint32_t max_number_of_samples)
  : mapFormat(map_format)
  , extent(extent)
  , resolution(res)
  , maxNumberOfSamples(max_number_of_samples)
{
}

void TerrainGeneratorModule::allocateResources()
{
  auto& ctx = etna::get_context();

  terrainMap = ctx.createImage(etna::Image::CreateInfo{
    .extent = extent,
    .name = "terrain_map",
    .format = mapFormat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});
  terrainNormalMap = ctx.createImage(etna::Image::CreateInfo{
    .extent = extent,
    .name = "terrain_normal_map",
    .format = vk::Format::eR8G8B8A8Snorm,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});

  generationParamsBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(TerrainGenerationParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = fmt::format("generationConstants{}", i)});
  });

  oneShotCommands = ctx.createOneShotCmdMgr();

  terrainSampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "terrain_sampler"});

  generationParams = {
    .extent = {extent.width, extent.height}, .numberOfSamples = 3, .persistence = 0.5};
}

void TerrainGeneratorModule::loadShaders()
{
  etna::create_program(
    "terrain_generator",
    {TERRAIN_GENERATOR_MODULE_SHADERS_ROOT "decoy.vert.spv",
     TERRAIN_GENERATOR_MODULE_SHADERS_ROOT "generator.frag.spv"});
  etna::create_program(
    "terrain_normal_map_calculation",
    {TERRAIN_GENERATOR_MODULE_SHADERS_ROOT "calculate_normal.comp.spv"});
}

void TerrainGeneratorModule::setupPipelines()
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  terrainGenerationPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_generator",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {mapFormat},
      }});

  terrainNormalPipeline =
    pipelineManager.createComputePipeline("terrain_normal_map_calculation", {});
}

void TerrainGeneratorModule::execute()
{
  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    auto& currentGenerationConstants = generationParamsBuffer->get();
    currentGenerationConstants.map();
    std::memcpy(
      currentGenerationConstants.data(), &generationParams, sizeof(TerrainGenerationParams));
    currentGenerationConstants.unmap();

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    glm::uvec2 glmExtent = {extent.width, extent.height};

    {
      etna::RenderTargetState state(
        commandBuffer,
        {{}, {glmExtent.x, glmExtent.y}},
        {{terrainMap.get(), terrainMap.getView({})}},
        {});

      auto shaderInfo = etna::get_shader_program("terrain_generator");
      auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        commandBuffer,
        {etna::Binding{0, currentGenerationConstants.genBinding()}});

      auto vkSet = set.getVkSet();

      commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        terrainGenerationPipeline.getVkPipelineLayout(),
        0,
        1,
        &vkSet,
        0,
        nullptr);


      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics, terrainGenerationPipeline.getVkPipeline());

      commandBuffer.draw(3, 1, 0, 0);
    }

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageRead,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      commandBuffer,
      terrainNormalMap.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    {
      auto shaderInfo = etna::get_shader_program("terrain_normal_map_calculation");

      auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        commandBuffer,
        {etna::Binding{0, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)},
         etna::Binding{
           1, terrainNormalMap.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)}});

      auto vkSet = set.getVkSet();

      commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        terrainNormalPipeline.getVkPipelineLayout(),
        0,
        1,
        &vkSet,
        0,
        nullptr);

      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eCompute, terrainNormalPipeline.getVkPipeline());

      commandBuffer.pushConstants<glm::uvec2>(
        terrainNormalPipeline.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eCompute,
        0,
        {resolution});

      commandBuffer.dispatch((glmExtent.x + 31) / 32, (glmExtent.y + 31) / 32, 1);
    }

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      commandBuffer,
      terrainNormalMap.get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void TerrainGeneratorModule::drawGui()
{
  ImGui::Begin("Render Settings");

  static ImU32 numberOfSamplesMin = 1;
  static ImU32 numberOfSamplesMax = maxNumberOfSamples;
  static float persistenceMin = 0.0f;
  static float persistenceMax = 1.0f;

  if (ImGui::CollapsingHeader("Terrain Generation Settings"))
  {
    ImGui::SeparatorText("Generation parameters");
    ImGui::SliderScalar(
      "Number of samples",
      ImGuiDataType_U32,
      &generationParams.numberOfSamples,
      &numberOfSamplesMin,
      &numberOfSamplesMax,
      "%u");
    ImGui::SliderScalar(
      "Persistence",
      ImGuiDataType_Float,
      &generationParams.persistence,
      &persistenceMin,
      &persistenceMax,
      "%f");
    if (ImGui::Button("Regenerate Terrain"))
    {
      execute();
    }
  }

  ImGui::End();
}
