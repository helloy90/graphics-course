#include "WaterGeneratorModule.hpp"

#include <glm/gtc/integer.hpp>
#include <glm/exponential.hpp>

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>


WaterGeneratorModule::WaterGeneratorModule()
  : params(
      {.windDirection = shader_vec2(1, 1), .windSpeed = 10, .gravity = 9.81, .wavePeriod = 200})
{
}

void WaterGeneratorModule::allocateResources(uint32_t textures_extent)
{
  auto& ctx = etna::get_context();

  vk::Extent3D textureExtent = {textures_extent, textures_extent, 1};

  uint32_t logExtent = static_cast<uint32_t>(glm::floor(glm::log2(textures_extent)));
  vk::Extent3D logTextureExtent = {logExtent, textures_extent, 1};

  initialSpectrumTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "initial_spectrum_tex",
    .format = vk::Format::eR32G32B32A32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});

  updatedSpectrumTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "updated_spectrum_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumSlopeXTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "updated_spectrum_slope_x_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumSlopeZTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "updated_spectrum_slope_z_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumDisplacementXTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "updated_spectrum_displacement_x_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumDisplacementZTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "updated_spectrum_displacement_z_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});

  twiddleFactorsTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = logTextureExtent,
    .name = "twiddle_factors_tex",
    .format = vk::Format::eR32G32B32A32Sfloat,
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
    "water_twiddle_factors_precompute",
    {WATER_GENERATOR_MODULE_SHADERS_ROOT "precompute_twiddle_factors.comp.spv"});

  etna::create_program(
    "water_spectrum_progression",
    {WATER_GENERATOR_MODULE_SHADERS_ROOT "update_spectrum_for_fft.comp.spv"});
}

void WaterGeneratorModule::setupPipelines()
{
  initialSpectrumGenerationPipeline =
    etna::get_context().getPipelineManager().createComputePipeline("water_spectrum_generation", {});
  twiddleFactorsPrecomputePipeline = etna::get_context().getPipelineManager().createComputePipeline(
    "water_twiddle_factors_precompute", {});

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
      initialSpectrumTexture.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);
    etna::set_state(
      commandBuffer,
      twiddleFactorsTexture.get(),
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
    {
      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eCompute, twiddleFactorsPrecomputePipeline.getVkPipeline());
      precomputeTwiddleFactors(
        commandBuffer, twiddleFactorsPrecomputePipeline.getVkPipelineLayout());
    }
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void WaterGeneratorModule::executeProgress(vk::CommandBuffer cmd_buf, float time)
{
  etna::set_state(
    cmd_buf,
    updatedSpectrumTexture.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumSlopeXTexture.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumSlopeZTexture.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumDisplacementXTexture.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumDisplacementZTexture.get(),
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
  auto extent = initialSpectrumTexture.getExtent();
  auto shaderInfo = etna::get_shader_program("water_spectrum_generation");

  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{
        0, initialSpectrumTexture.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{1, paramsBuffer.genBinding()},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.dispatch((extent.width + 31) / 32, (extent.height + 31) / 32, 1);
}

void WaterGeneratorModule::precomputeTwiddleFactors(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  auto extent = twiddleFactorsTexture.getExtent();
  auto shaderInfo = etna::get_shader_program("water_twiddle_factors_precompute");

  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{
        0, twiddleFactorsTexture.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{1, paramsBuffer.genBinding()},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  // other half computed in shader
  cmd_buf.dispatch((extent.width + 31) / 32, (extent.height + 63) / 64, 1);
}

void WaterGeneratorModule::updateSpectrumForFFT(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, float time)
{
  auto extent = initialSpectrumTexture.getExtent();
  auto shaderInfo = etna::get_shader_program("water_twiddle_factors_precompute");

  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{
        0, initialSpectrumTexture.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        1, updatedSpectrumTexture.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        2,
        updatedSpectrumSlopeXTexture.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        3,
        updatedSpectrumSlopeZTexture.genBinding(spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        4,
        updatedSpectrumDisplacementXTexture.genBinding(
          spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        5,
        updatedSpectrumDisplacementZTexture.genBinding(
          spectrumSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{6, paramsBuffer.genBinding()},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants<float>(pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, {time});

  cmd_buf.dispatch((extent.width + 31) / 32, (extent.height + 31) / 32, 1);
}
