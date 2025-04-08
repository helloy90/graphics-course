#include "WaterGeneratorModule.hpp"
#include "etna/GlobalContext.hpp"

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <vulkan/vulkan_enums.hpp>


WaterGeneratorModule::WaterGeneratorModule() {}

void WaterGeneratorModule::allocateResources(
  vk::Format spectrum_image_format, vk::Extent3D spectrum_image_extent)
{
  auto& ctx = etna::get_context();

  spectrumImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = spectrum_image_extent,
    .name = "spectrum_map",
    .format = spectrum_image_format,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});

  paramsBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(SpectrumGenerationParams),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO,
    .allocationCreate =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .name = "SpectrumGenerationParams"});

  oneShotCommands = ctx.createOneShotCmdMgr();

  spectrumSampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "spectrum_sampler"});
}

void WaterGeneratorModule::loadShaders()
{
  etna::create_program(
    "water_spectrum_generation",
    {WATER_GENERATOR_MODULE_SHADERS_ROOT "generate_spectrum.comp.spv"});
}

void WaterGeneratorModule::setupPipelines()
{
  spectrumGenerationPipeline =
    etna::get_context().getPipelineManager().createComputePipeline("water_spectrum_generation", {});
}

void WaterGeneratorModule::execute()
{
  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    paramsBuffer.map();
    std::memcpy(paramsBuffer.data(), &params, sizeof(spectrumGenerationPipeline));
    paramsBuffer.unmap();

    etna::set_state(
      commandBuffer,
      spectrumImage.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    {
      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eCompute, spectrumGenerationPipeline.getVkPipeline());
      generateSpectrum(commandBuffer, spectrumGenerationPipeline.getVkPipelineLayout());
    }
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void WaterGeneratorModule::generateSpectrum(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  auto extent = spectrumImage.getExtent();
  auto shaderInfo = etna::get_shader_program("water_spectrum_generation");

  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{0, spectrumImage.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  //   cmd_buf.pushConstants<glm::uvec2>(
  //     pipeline_layout,
  //     vk::ShaderStageFlagBits::eCompute,
  //     0,
  //     {normal_map_fidelity});

  cmd_buf.dispatch((extent.width + 31) / 32, (extent.height + 31) / 32, 1);
}
