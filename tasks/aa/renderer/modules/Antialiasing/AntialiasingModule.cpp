#include "AntialiasingModule.hpp"
#include "render_utils/Utilities.hpp"

#include <etna/GlobalContext.hpp>
#include <vulkan/vulkan_structs.hpp>


AntialiasingModule::AntialiasingModule(const CreateInfo& info)
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
}

void AntialiasingModule::getPreviousImages(
  vk::CommandBuffer cmd_buf,
  [[maybe_unused]] const etna::Image& prev_target,
  [[maybe_unused]] const etna::Image& prev_depth)
{
  vk::Extent3D extent = prev_target.getExtent();

  render_utility::blit_image(
    cmd_buf,
    prev_target.get(),
    previousTargetImage.get(),
    {.x = static_cast<int32_t>(extent.width), .y = static_cast<int32_t>(extent.height), .z = 1});

  render_utility::blit_image(
    cmd_buf,
    prev_depth.get(),
    previousDepthImage.get(),
    {.x = static_cast<int32_t>(extent.width), .y = static_cast<int32_t>(extent.height), .z = 1});
}

void AntialiasingModule::execute([[maybe_unused]]vk::CommandBuffer cmd_buf, [[maybe_unused]]const etna::Image& render_target)
{
  // skip TAA only for the first frame
  if (!enable) [[unlikely]]
  {
    enable = true;
    return;
  }
}
