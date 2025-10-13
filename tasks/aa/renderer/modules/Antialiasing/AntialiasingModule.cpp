#include "AntialiasingModule.hpp"

#include <glm/ext/matrix_transform.hpp>
#include <tracy/Tracy.hpp>
#include <imgui.h>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>

#include <render_utils/Utilities.hpp>


AntialiasingModule::AntialiasingModule()
  : params(
      {.currentProjView = glm::identity<glm::mat4>(),
       .currentInvProjView = glm::identity<glm::mat4>(),
       .previousProjView = glm::identity<glm::mat4>(),
       .depthCutoff = 0.1f,
       .previousFrameUsage = 0.9f})
  , jitterIndex(0)
  , currentJitter(0.0f, 0.0f)
  , previousJitter(0.0f, 0.0f)
{
}

void AntialiasingModule::allocateResources(const AllocationInfo& info)
{
  auto& ctx = etna::get_context();

  vk::Extent3D renderImagesExtent = {info.resolution.x, info.resolution.y, 1};

  previousTargetImage = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = renderImagesExtent,
      .name = "previousTargetImage",
      .format = info.renderTargetFormat,
      .imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  previousDepthImage = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = renderImagesExtent,
      .name = "previousDepthImage",
      .format = info.depthFormat,
      .imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  paramsBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(
      etna::Buffer::CreateInfo{
        .size = sizeof(AntialiasingParams),
        .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_AUTO,
        .allocationCreate =
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .name = fmt::format("antialiasingConstants{}", i)});
  });

  depthSampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "taaDepthSampler"});
}

void AntialiasingModule::loadShaders()
{
  etna::create_program("taa", {ANTIALIASING_MODULE_SHADERS_ROOT "taa.comp.spv"});
}

void AntialiasingModule::setupPipelines()
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  aaPipeline = pipelineManager.createComputePipeline("taa", {});
}


void AntialiasingModule::setBarriersForCopy(vk::CommandBuffer cmd_buf)
{
  etna::set_state(
    cmd_buf,
    previousTargetImage.get(),
    vk::PipelineStageFlagBits2::eTransfer,
    vk::AccessFlagBits2::eTransferWrite,
    vk::ImageLayout::eTransferDstOptimal,
    vk::ImageAspectFlagBits::eColor);

  etna::set_state(
    cmd_buf,
    previousDepthImage.get(),
    vk::PipelineStageFlagBits2::eTransfer,
    vk::AccessFlagBits2::eTransferWrite,
    vk::ImageLayout::eTransferDstOptimal,
    vk::ImageAspectFlagBits::eDepth);
}

void AntialiasingModule::setBarriersForExecute(vk::CommandBuffer cmd_buf)
{
  etna::set_state(
    cmd_buf,
    previousTargetImage.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderStorageRead,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);

  etna::set_state(
    cmd_buf,
    previousDepthImage.get(),
    vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderSampledRead,
    vk::ImageLayout::eShaderReadOnlyOptimal,
    vk::ImageAspectFlagBits::eDepth);
}

void AntialiasingModule::execute(
  vk::CommandBuffer cmd_buf,
  const etna::Image& render_target,
  const etna::Image& depth_image,
  const glm::mat4& proj_view,
  const glm::mat4& inv_proj_view)
{
  ZoneScoped;

  // skip TAA only for the first frame
  if (!enable) [[unlikely]]
  {
    enable = true;
    return;
  }

  params.currentProjView = proj_view;
  params.currentInvProjView = inv_proj_view;

  auto& currentConstants = paramsBuffer->get();
  currentConstants.map();
  std::memcpy(currentConstants.data(), &params, sizeof(AntialiasingParams));
  currentConstants.unmap();

  auto extent = render_target.getExtent();

  {
    ETNA_PROFILE_GPU(cmd_buf, temporalAA);

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, aaPipeline.getVkPipeline());

    auto shaderInfo = etna::get_shader_program("taa");

    auto set = etna::create_descriptor_set(
      shaderInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{0, previousTargetImage.genBinding({}, vk::ImageLayout::eGeneral)},
       etna::Binding{
         1,
         previousDepthImage.genBinding(
           depthSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
       etna::Binding{2, render_target.genBinding({}, vk::ImageLayout::eGeneral)},
       etna::Binding{
         3, depth_image.genBinding(depthSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
       etna::Binding{4, currentConstants.genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute, aaPipeline.getVkPipelineLayout(), 0, {set.getVkSet()}, {});

    cmd_buf.dispatch(
      (static_cast<uint32_t>(extent.width) + 31) / 32,
      (static_cast<uint32_t>(extent.height) + 31) / 32,
      1);
  }
}

void AntialiasingModule::copyPreviousData(
  vk::CommandBuffer cmd_buf,
  const etna::Image& prev_target,
  const etna::Image& prev_depth_image,
  const glm::mat4& previous_proj_view)
{
  vk::Extent3D extent = prev_target.getExtent();

  render_utility::blit_image(
    cmd_buf,
    prev_target.get(),
    previousTargetImage.get(),
    vk::ImageAspectFlagBits::eColor,
    {.x = static_cast<int32_t>(extent.width), .y = static_cast<int32_t>(extent.height), .z = 1});

  render_utility::blit_image(
    cmd_buf,
    prev_depth_image.get(),
    previousDepthImage.get(),
    vk::ImageAspectFlagBits::eDepth,
    {.x = static_cast<int32_t>(extent.width), .y = static_cast<int32_t>(extent.height), .z = 1});

  params.previousProjView = previous_proj_view;
}

void AntialiasingModule::drawGui()
{

  ImGui::Begin("Application Settings");

  if (ImGui::CollapsingHeader("Temporal Antialiasing"))
  {
    ImGui::DragFloat("Depth delta cutoff", &params.depthCutoff, 0.001f, 0.0f, 1.0f);
    ImGui::DragFloat("Previous frame usage", &params.previousFrameUsage, 0.001f, 0.0f, 1.0f);
  }

  ImGui::End();
}

void AntialiasingModule::updateJitter()
{
  previousJitter = currentJitter;

  float haltonX = (2.0f * haltonJitter(jitterIndex + 1, 2) - 1.0f) / 2;
  float haltonY = (2.0f * haltonJitter(jitterIndex + 1, 3) - 1.0f) / 2;

  jitterIndex++;
  jitterIndex = jitterIndex % 8; // maybe change 8 to some parameter

  auto extent = previousTargetImage.getExtent();

  glm::vec2 resolution = glm::vec2(extent.width, extent.height);
  currentJitter = glm::vec2(haltonX, haltonY) / resolution;
}

float AntialiasingModule::haltonJitter(uint32_t index, uint32_t base)
{
  float result = 0.0f;
  float current = 1.0f;

  while (index > 0)
  {
    current /= static_cast<float>(base);
    result += current * static_cast<float>(index % base);
    index = static_cast<uint32_t>(glm::floor(static_cast<float>(index) / static_cast<float>(base)));
  }

  return result;
}
