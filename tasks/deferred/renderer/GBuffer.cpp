#include "GBuffer.hpp"

#include <vector>

#include <etna/DescriptorSet.hpp>
#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>


GBuffer::GBuffer(glm::uvec2 resolution, vk::Format render_target_format)
{

  auto& ctx = etna::get_context();

  colorImages[0] = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "albedo",
    .format = render_target_format,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
      vk::ImageUsageFlagBits::eStorage,
  });

  colorImages[1] = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "normal",
    .format = render_target_format,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
      vk::ImageUsageFlagBits::eStorage,
  });

  depth = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
  });
}

void GBuffer::prepareForRender(vk::CommandBuffer cmd_buf)
{

  for (const auto& image : colorImages)
  {
    etna::set_state(
      cmd_buf,
      image.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);
  }

  // etna::set_state(
  //   cmd_buf,
  //   normal.get(),
  //   vk::PipelineStageFlagBits2::eColorAttachmentOutput,
  //   vk::AccessFlagBits2::eColorAttachmentWrite,
  //   vk::ImageLayout::eColorAttachmentOptimal,
  //   vk::ImageAspectFlagBits::eColor);
  // etna::set_state(
  //   cmd_buf,
  //   albedo.get(),
  //   vk::PipelineStageFlagBits2::e,
  //   vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
  //   vk::ImageLayout::eDepthAttachmentOptimal,
  //   vk::ImageAspectFlagBits::eDepth);
}

void GBuffer::prepareForRead(vk::CommandBuffer cmd_buf)
{

  for (const auto& image : colorImages)
  {
    etna::set_state(
      cmd_buf,
      image.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);
  }

  // etna::set_state(
  //   cmd_buf,
  //   albedo.get(),
  //   vk::PipelineStageFlagBits2::eFragmentShader,
  //   vk::AccessFlagBits2::eShaderSampledRead,
  //   vk::ImageLayout::eReadOnlyOptimal,
  //   vk::ImageAspectFlagBits::eColor);
  // etna::set_state(
  //   cmd_buf,
  //   normal.get(),
  //   vk::PipelineStageFlagBits2::eFragmentShader,
  //   vk::AccessFlagBits2::eShaderSampledRead,
  //   vk::ImageLayout::eReadOnlyOptimal,
  //   vk::ImageAspectFlagBits::eColor);
}

std::vector<etna::RenderTargetState::AttachmentParams> GBuffer::genColorAttachmentParams()
{
  // little bit ugly
  return {
    {.image = colorImages[0].get(), .view = colorImages[0].getView({})},
    {.image = colorImages[1].get(), .view = colorImages[1].getView({})}};
}

etna::RenderTargetState::AttachmentParams GBuffer::genDepthAttachemtParams()
{
  return {.image = depth.get(), .view = depth.getView({})};
}

std::vector<etna::Binding> GBuffer::genBindings()
{
  return {
    etna::Binding{0, colorImages[0].genBinding({}, vk::ImageLayout::eShaderReadOnlyOptimal)},
    etna::Binding{1, colorImages[1].genBinding({}, vk::ImageLayout::eShaderReadOnlyOptimal)}};
}
