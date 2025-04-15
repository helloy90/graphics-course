#include "Utilities.hpp"

namespace render_utility
{

void local_copy_buffer_to_image(
  etna::OneShotCmdMgr& one_shot_cmd_mgr,
  const etna::Buffer& buffer,
  const etna::Image& image,
  uint32_t layer_count)
{
  auto commandBuffer = one_shot_cmd_mgr.start();

  auto extent = image.getExtent();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    etna::set_state(
      commandBuffer,
      image.get(),
      vk::PipelineStageFlagBits2::eTransfer,
      vk::AccessFlagBits2::eTransferWrite,
      vk::ImageLayout::eTransferDstOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    vk::BufferImageCopy copyRegion = {
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource =
        {.aspectMask = vk::ImageAspectFlagBits::eColor,
         .mipLevel = 0,
         .baseArrayLayer = 0,
         .layerCount = layer_count},
      .imageExtent =
        vk::Extent3D{static_cast<uint32_t>(extent.width), static_cast<uint32_t>(extent.height), 1}};

    commandBuffer.copyBufferToImage(
      buffer.get(), image.get(), vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  one_shot_cmd_mgr.submitAndWait(commandBuffer);
}

void generate_mipmaps_vk_style(
  etna::OneShotCmdMgr& one_shot_cmd_mgr,
  const etna::Image& image,
  uint32_t mip_levels,
  uint32_t layer_count)
{
  auto extent = image.getExtent();

  auto commandBuffer = one_shot_cmd_mgr.start();

  auto vkImage = image.get();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    int32_t mipWidth = extent.width;
    int32_t mipHeight = extent.height;

    vk::ImageMemoryBarrier barrier{
      .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
      .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
      .image = vkImage,
      .subresourceRange = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = layer_count,
      }};

    for (uint32_t i = 1; i < mip_levels; i++)
    {
      barrier.subresourceRange.baseMipLevel = i - 1;
      barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
      barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

      commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eTransfer,
        vk::DependencyFlagBits::eByRegion,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);

      std::array srcOffset = {vk::Offset3D{0, 0, 0}, vk::Offset3D{mipWidth, mipHeight, 1}};

      auto srcImageSubrecourceLayers = vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = i - 1,
        .baseArrayLayer = 0,
        .layerCount = layer_count};

      std::array dstOffset = {
        vk::Offset3D{0, 0, 0},
        vk::Offset3D{mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1}};

      auto dstImageSubrecourceLayers = vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = i,
        .baseArrayLayer = 0,
        .layerCount = layer_count};

      auto imageBlit = vk::ImageBlit{
        .srcSubresource = srcImageSubrecourceLayers,
        .srcOffsets = srcOffset,
        .dstSubresource = dstImageSubrecourceLayers,
        .dstOffsets = dstOffset};

      commandBuffer.blitImage(
        vkImage,
        vk::ImageLayout::eTransferSrcOptimal,
        vkImage,
        vk::ImageLayout::eTransferDstOptimal,
        1,
        &imageBlit,
        vk::Filter::eLinear);

      barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
      barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

      commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eTransfer,
        vk::DependencyFlagBits::eByRegion,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);

      if (mipWidth > 1)
      {
        mipWidth /= 2;
      }
      if (mipHeight > 1)
      {
        mipHeight /= 2;
      }
    }

    etna::set_state(
      commandBuffer,
      image.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  one_shot_cmd_mgr.submitAndWait(commandBuffer);
}

// assume images have the same resolution
void blit_image(
  vk::CommandBuffer cmd_buf,
  vk::Image source_image,
  vk::Image target_image,
  vk::Offset3D offset_size)
{
  std::array srcOffset = {vk::Offset3D{}, offset_size};
  auto srdImageSubrecourceLayers = vk::ImageSubresourceLayers{
    .aspectMask = vk::ImageAspectFlagBits::eColor,
    .mipLevel = 0,
    .baseArrayLayer = 0,
    .layerCount = 1};

  std::array dstOffset = {vk::Offset3D{}, offset_size};
  auto dstImageSubrecourceLayers = vk::ImageSubresourceLayers{
    .aspectMask = vk::ImageAspectFlagBits::eColor,
    .mipLevel = 0,
    .baseArrayLayer = 0,
    .layerCount = 1};

  auto imageBlit = vk::ImageBlit2{
    .sType = vk::StructureType::eImageBlit2,
    .pNext = nullptr,
    .srcSubresource = srdImageSubrecourceLayers,
    .srcOffsets = srcOffset,
    .dstSubresource = dstImageSubrecourceLayers,
    .dstOffsets = dstOffset};

  auto blitInfo = vk::BlitImageInfo2{
    .sType = vk::StructureType::eBlitImageInfo2,
    .pNext = nullptr,
    .srcImage = source_image,
    .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
    .dstImage = target_image,
    .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
    .regionCount = 1,
    .pRegions = &imageBlit,
    .filter = vk::Filter::eLinear};

  cmd_buf.blitImage2(&blitInfo);
}

} // namespace render_utility
