#include "WaterGeneratorModule.hpp"

#include <imgui.h>

#include <glm/gtc/integer.hpp>
#include <glm/exponential.hpp>

#include <etna/Etna.hpp>
#include <etna/Profiling.hpp>
#include <etna/PipelineManager.hpp>


WaterGeneratorModule::WaterGeneratorModule()
  : params(
      {.windDirection = shader_vec2(1, 1), .windSpeed = 10, .wavePeriod = 200, .gravity = 9.81})
{
}

void WaterGeneratorModule::allocateResources(uint32_t textures_extent)
{
  auto& ctx = etna::get_context();

  vk::Extent3D textureExtent = {textures_extent, textures_extent, 1};

  uint32_t logExtent = static_cast<uint32_t>(glm::log2(textures_extent));

  info = {.size = textures_extent, .logSize = logExtent, .texturesAmount = 5};

  initialSpectrumTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "initial_spectrum_tex",
    .format = vk::Format::eR32G32B32A32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eStorage});

  updatedSpectrumTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "updated_spectrum_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumSlopeXTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "updated_spectrum_slope_x_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumSlopeZTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "updated_spectrum_slope_z_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumDisplacementXTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "updated_spectrum_displacement_x_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eStorage});
  updatedSpectrumDisplacementZTexture = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "updated_spectrum_displacement_z_tex",
    .format = vk::Format::eR32G32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eStorage});

  heightMap = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "water_height_map",
    .format = vk::Format::eR32G32B32A32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});
  normalMap = ctx.createImage(etna::Image::CreateInfo{
    .extent = textureExtent,
    .name = "water_normal_map",
    .format = vk::Format::eR32G32B32A32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});

  paramsBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(SpectrumGenerationParams),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO,
    .allocationCreate =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .name = "spectrumGenerationParams"});
  infoBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(InverseFFTInfo),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO,
    .allocationCreate =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .name = "inverseFFTInfo"});

  oneShotCommands = ctx.createOneShotCmdMgr();

  textureSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eRepeat,
    .name = "spectrum_sampler"});
}

void WaterGeneratorModule::loadShaders()
{
  etna::create_program(
    "water_spectrum_generation",
    {WATER_GENERATOR_MODULE_SHADERS_ROOT "generate_initial_spectrum.comp.spv"});

  etna::create_program(
    "water_spectrum_progression",
    {WATER_GENERATOR_MODULE_SHADERS_ROOT "update_spectrum_for_fft.comp.spv"});
  etna::create_program(
    "water_horizontal_inverse_fft",
    {WATER_GENERATOR_MODULE_SHADERS_ROOT "horizontal_inverse_fft.comp.spv"});
  etna::create_program(
    "water_vertical_inverse_fft",
    {WATER_GENERATOR_MODULE_SHADERS_ROOT "vertical_inverse_fft.comp.spv"});

  etna::create_program(
    "water_assembler", {WATER_GENERATOR_MODULE_SHADERS_ROOT "assemble.comp.spv"});
}

void WaterGeneratorModule::setupPipelines()
{
  initialSpectrumGenerationPipeline =
    etna::get_context().getPipelineManager().createComputePipeline("water_spectrum_generation", {});

  spectrumProgressionPipeline = etna::get_context().getPipelineManager().createComputePipeline(
    "water_spectrum_progression", {});

  horizontalInverseFFTPipeline = etna::get_context().getPipelineManager().createComputePipeline(
    "water_horizontal_inverse_fft", {});
  verticalInverseFFTPipeline = etna::get_context().getPipelineManager().createComputePipeline(
    "water_vertical_inverse_fft", {});

  assemblerPipeline =
    etna::get_context().getPipelineManager().createComputePipeline("water_assembler", {});
}

void WaterGeneratorModule::executeStart()
{
  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    paramsBuffer.map();
    std::memcpy(paramsBuffer.data(), &params, sizeof(SpectrumGenerationParams));
    paramsBuffer.unmap();

    infoBuffer.map();
    std::memcpy(infoBuffer.data(), &info, sizeof(InverseFFTInfo));
    infoBuffer.unmap();

    etna::set_state(
      commandBuffer,
      initialSpectrumTexture.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    {
      ETNA_PROFILE_GPU(commandBuffer, generateInitialSpectrum)
      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eCompute, initialSpectrumGenerationPipeline.getVkPipeline());
      generateInitialSpectrum(
        commandBuffer, initialSpectrumGenerationPipeline.getVkPipelineLayout());
    }

    auto bindings = {
      etna::Binding{
        0, updatedSpectrumTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral), 0},
      etna::Binding{
        0,
        updatedSpectrumSlopeXTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral),
        1},
      etna::Binding{
        0,
        updatedSpectrumSlopeZTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral),
        2},
      etna::Binding{
        0,
        updatedSpectrumDisplacementXTexture.genBinding(
          textureSampler.get(), vk::ImageLayout::eGeneral),
        3},
      etna::Binding{
        0,
        updatedSpectrumDisplacementZTexture.genBinding(
          textureSampler.get(), vk::ImageLayout::eGeneral),
        4}};

    auto shaderInfoHorizontal = etna::get_shader_program("water_horizontal_inverse_fft");
    horizontalInverseFFTDescriptorSet = etna::create_persistent_descriptor_set(
      shaderInfoHorizontal.getDescriptorLayoutId(0), bindings, true);
    horizontalInverseFFTDescriptorSet->processBarriers(commandBuffer);

    auto shaderInfoVertical = etna::get_shader_program("water_vertical_inverse_fft");
    verticalIInverseFFTDescriptorSet = etna::create_persistent_descriptor_set(
      shaderInfoVertical.getDescriptorLayoutId(0), bindings, true);
    verticalIInverseFFTDescriptorSet->processBarriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void WaterGeneratorModule::executeProgress(vk::CommandBuffer cmd_buf, float time)
{
  ETNA_PROFILE_GPU(cmd_buf, waterProgress);
  etna::set_state(
    cmd_buf,
    updatedSpectrumTexture.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumSlopeXTexture.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumSlopeZTexture.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumDisplacementXTexture.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    updatedSpectrumDisplacementZTexture.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);

  etna::flush_barriers(cmd_buf);

  {
    ETNA_PROFILE_GPU(cmd_buf, updateSpectrumForFFT);
    cmd_buf.bindPipeline(
      vk::PipelineBindPoint::eCompute, spectrumProgressionPipeline.getVkPipeline());
    updateSpectrumForFFT(cmd_buf, spectrumProgressionPipeline.getVkPipelineLayout(), time);
  }

  {
    ETNA_PROFILE_GPU(cmd_buf, inverseFFT);
    inverseFFT(cmd_buf);
  }

  etna::set_state(
    cmd_buf,
    heightMap.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    normalMap.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageWrite,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);

  etna::flush_barriers(cmd_buf);

  {
    ETNA_PROFILE_GPU(cmd_buf, assembleMaps);
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, assemblerPipeline.getVkPipeline());
    assembleMaps(cmd_buf, assemblerPipeline.getVkPipelineLayout());
  }
}

void WaterGeneratorModule::drawGui()
{
  ImGui::Begin("Application Settings");

  static bool paramsChanged = false;

  if (ImGui::CollapsingHeader("Water Generator"))
  {
    ImGui::SeparatorText("Generation parameters");

    float windDirection[] = {params.windDirection.x, params.windDirection.y};
    float windSpeed = params.windSpeed;
    float wavePeriod = params.wavePeriod;
    float gravity = params.gravity;

    paramsChanged = paramsChanged || ImGui::DragFloat("Wave Period", &wavePeriod, 1, 0.00001, 5000);
    params.wavePeriod = wavePeriod;
    paramsChanged = paramsChanged || ImGui::DragFloat("Gravity", &gravity, 0.1, 0, 5000);
    params.gravity = gravity;

    ImGui::Text("Water regeneration needed for these to take effect");
    paramsChanged = paramsChanged || ImGui::DragFloat2("Wind Direction", windDirection, 0.1);
    params.windDirection = glm::vec2(windDirection[0], windDirection[1]);
    paramsChanged = paramsChanged || ImGui::DragFloat("Wind Speed", &windSpeed, 0.1, 0.0, 5000);
    params.windSpeed = windSpeed;

    if (ImGui::Button("Regenerate Water"))
    {
      executeStart();
    }
  }

  if (paramsChanged)
  {
    paramsBuffer.map();
    std::memcpy(paramsBuffer.data(), &params, sizeof(SpectrumGenerationParams));
    paramsBuffer.unmap();
    paramsChanged = false;
  }

  ImGui::End();
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
        0, initialSpectrumTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{1, paramsBuffer.genBinding()},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.dispatch((extent.width + 31) / 32, (extent.height + 31) / 32, 1);
}

void WaterGeneratorModule::updateSpectrumForFFT(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, float time)
{
  auto extent = initialSpectrumTexture.getExtent();
  auto shaderInfo = etna::get_shader_program("water_spectrum_progression");

  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{
        0, initialSpectrumTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        1, updatedSpectrumTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        2,
        updatedSpectrumSlopeXTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        3,
        updatedSpectrumSlopeZTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        4,
        updatedSpectrumDisplacementXTexture.genBinding(
          textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        5,
        updatedSpectrumDisplacementZTexture.genBinding(
          textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{6, paramsBuffer.genBinding()},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants<float>(pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, {time});

  cmd_buf.dispatch((extent.width + 31) / 32, (extent.height + 31) / 32, 1);
}

void WaterGeneratorModule::inverseFFT(vk::CommandBuffer cmd_buf)
{

  {
    ETNA_PROFILE_GPU(cmd_buf, inverseFFTHorizontalStep);
    cmd_buf.bindPipeline(
      vk::PipelineBindPoint::eCompute, horizontalInverseFFTPipeline.getVkPipeline());
    executeInverseFFT(
      cmd_buf,
      horizontalInverseFFTPipeline.getVkPipelineLayout(),
      "water_horizontal_inverse_fft",
      *horizontalInverseFFTDescriptorSet,
      {1, info.size, 1});
  }

  {
    ETNA_PROFILE_GPU(cmd_buf, inverseFFTVerticalStep);
    cmd_buf.bindPipeline(
      vk::PipelineBindPoint::eCompute, verticalInverseFFTPipeline.getVkPipeline());
    executeInverseFFT(
      cmd_buf,
      verticalInverseFFTPipeline.getVkPipelineLayout(),
      "water_vertical_inverse_fft",
      *verticalIInverseFFTDescriptorSet,
      {info.size, 1, 1});
  }
}

void WaterGeneratorModule::executeInverseFFT(
  vk::CommandBuffer cmd_buf,
  vk::PipelineLayout pipeline_layout,
  const char* shader_program,
  etna::PersistentDescriptorSet persistent_set,
  vk::Extent3D extent)
{
  auto shaderInfo = etna::get_shader_program(shader_program);

  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(1),
    cmd_buf,
    {etna::Binding{0, paramsBuffer.genBinding()}, etna::Binding{1, infoBuffer.genBinding()}});

  auto vkSet = set.getVkSet();


  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, {persistent_set.getVkSet(), vkSet}, {});

  cmd_buf.dispatch(extent.width, extent.height, 1);
}

void WaterGeneratorModule::assembleMaps(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout)
{
  auto extent = initialSpectrumTexture.getExtent();
  auto shaderInfo = etna::get_shader_program("water_assembler");

  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {
      etna::Binding{
        0, updatedSpectrumTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        1,
        updatedSpectrumSlopeXTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        2,
        updatedSpectrumSlopeZTexture.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        3,
        updatedSpectrumDisplacementXTexture.genBinding(
          textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{
        4,
        updatedSpectrumDisplacementZTexture.genBinding(
          textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{5, heightMap.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{6, normalMap.genBinding(textureSampler.get(), vk::ImageLayout::eGeneral)},
      etna::Binding{7, paramsBuffer.genBinding()},
    });

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.dispatch((extent.width + 31) / 32, (extent.height + 31) / 32, 1);
}
