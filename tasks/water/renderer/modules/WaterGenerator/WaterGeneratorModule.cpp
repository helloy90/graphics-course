#include "WaterGeneratorModule.hpp"
#include "shaders/SpectrumGenerationParams.h"

#include <cstddef>
#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>


WaterGeneratorModule::WaterGeneratorModule()
  : params(
      {.windDirection = shader_vec2(1, 1), .windSpeed = 10, .gravity = 9.81, .wavePeriod = 200})
{
}

void WaterGeneratorModule::allocateResources(vk::Extent3D spectrum_image_extent)
{
  auto& ctx = etna::get_context();

  initialSpectrumImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = spectrum_image_extent,
    .name = "initial_spectrum_tex",
    .format = vk::Format::eR32G32B32A32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});

  updatedSpectrumImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = spectrum_image_extent,
    .name = "updated_spectrum_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumSlopeXImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = spectrum_image_extent,
    .name = "updated_spectrum_slope_x_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumSlopeZImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = spectrum_image_extent,
    .name = "updated_spectrum_slope_z_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumDisplacementXImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = spectrum_image_extent,
    .name = "updated_spectrum_displacement_x_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumDisplacementZImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = spectrum_image_extent,
    .name = "updated_spectrum_displacement_z_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});

  paramsBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(SpectrumGenerationParams),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO,
    .allocationCreate =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .name = "spectrumGenerationParams"});

  oneShotCommands = ctx.createOneShotCmdMgr();

  spectrumSampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "spectrum_sampler"});
}

void WaterGeneratorModule::loadShaders()
{
  etna::create_program(
    "water_spectrum_generation",
    {WATER_GENERATOR_MODULE_SHADERS_ROOT "generate_initial_spectrum.comp.spv"});
  etna::create_program(
    "water_spectrum_progression",
    {WATER_GENERATOR_MODULE_SHADERS_ROOT "update_spectrum_for_fft.comp.spv"});
}

void WaterGeneratorModule::setupPipelines()
{
  initialSpectrumGenerationPipeline =
    etna::get_context().getPipelineManager().createComputePipeline("water_spectrum_generation", {});
  spectrumProgressionPipeline = etna::get_context().getPipelineManager().createComputePipeline(
    "water_spectrum_progression", {});
}

void WaterGeneratorModule::executeStart()
{
  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    paramsBuffer.map();
    std::memcpy(paramsBuffer.data(), &params, sizeof(SpectrumGenerationParams));
    paramsBuffer.unmap();

    etna::set_state(
      commandBuffer,
      initialSpectrumImage.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    {
      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eCompute, initialSpectrumGenerationPipeline.getVkPipeline());
      generateInitialSpectrum(
        commandBuffer, initialSpectrumGenerationPipeline.getVkPipelineLayout());
    }
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void WaterGeneratorModule::executeProgress(vk::CommandBuffer cmd_buf, float time)
{
  etna::set_state(
    cmd_buf,
    updatedSpectrumImage.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumSlopeXImage.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumSlopeZImage.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumDisplacementXImage.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumDisplacementZImage.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);


  etna::flush_barriers(cmd_buf);

  {
    cmd_buf.bindPipeline(
      vk::PipelineBindPoint::eCompute, spectrumProgressionPipeline.getVkPipeline());
    updateSpectrumForFFT(cmd_buf, spectrumProgressionPipeline.getVkPipelineLayout(), time);
  }
}


void WaterGeneratorModule::generateInitialSpectrum(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  auto extent = initialSpectrumImage.getExtent();
  auto shaderInfo = etna::get_shader_program("water_spectrum_generation");

  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{
        0, initialSpectrumImage.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{1, paramsBuffer.genBinding()},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  //   cmd_buf.pushConstants<glm::uvec2>(
  //     pipeline_layout,
  //     vk::ShaderStageFlagBits::eCompute,
  //     0,
  //     {normal_tex_fidelity});

  cmd_buf.dispatch((extent.width + 31) / 32, (extent.height + 31) / 32, 1);
}

void WaterGeneratorModule::updateSpectrumForFFT(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, float time)
{
  auto extent = initialSpectrumImage.getExtent();
  auto shaderInfo = etna::get_shader_program("water_spectrum_progression");

  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{
        0, initialSpectrumImage.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        1, updatedSpectrumImage.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        2, updatedSpectrumSlopeXImage.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        3, updatedSpectrumSlopeZImage.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        4,
        updatedSpectrumDisplacementXImage.genBinding(
          spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        5,
        updatedSpectrumDisplacementZImage.genBinding(
          spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{6, paramsBuffer.genBinding()},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants<float>(pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, {time});

  cmd_buf.dispatch((extent.width + 31) / 32, (extent.height + 31) / 32, 1);
}
