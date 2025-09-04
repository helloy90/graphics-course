#include "GBuffer.hpp"
#include "etna/Image.hpp"

#include <etna/DescriptorSet.hpp>
#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>


GBuffer::GBuffer(glm::uvec2 resolution, vk::Format render_target_format)
{

  auto& ctx = etna::get_context();

  albedo = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "albedo",
      .format = render_target_format,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  normal = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "normal",
      .format = vk::Format::eR16G16B16A16Snorm,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  material = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "material",
      .format = vk::Format::eR8G8B8A8Unorm,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  depth = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "depth",
      .format = vk::Format::eD32Sfloat,
      .imageUsage =
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  shadows = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{2048, 2048, 1},
      .name = "shadows",
      .format = vk::Format::eD16Unorm,
      .imageUsage =
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  sampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "gBuffer_sampler"});
}

void GBuffer::prepareForRender(vk::CommandBuffer cmd_buf)
{
  etna::set_state(
    cmd_buf,
    albedo.get(),
    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    vk::AccessFlagBits2::eColorAttachmentWrite,
    vk::ImageLayout::eColorAttachmentOptimal,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    normal.get(),
    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    vk::AccessFlagBits2::eColorAttachmentWrite,
    vk::ImageLayout::eColorAttachmentOptimal,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    material.get(),
    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    vk::AccessFlagBits2::eColorAttachmentWrite,
    vk::ImageLayout::eColorAttachmentOptimal,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    depth.get(),
    vk::PipelineStageFlagBits2::eEarlyFragmentTests |
      vk::PipelineStageFlagBits2::eLateFragmentTests,
    vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
    vk::ImageLayout::eDepthStencilAttachmentOptimal,
    vk::ImageAspectFlagBits::eDepth);
}

void GBuffer::continueDepthWrite(vk::CommandBuffer cmd_buf)
{
  etna::set_state(
    cmd_buf,
    depth.get(),
    vk::PipelineStageFlagBits2::eEarlyFragmentTests |
      vk::PipelineStageFlagBits2::eLateFragmentTests,
    vk::AccessFlagBits2::eDepthStencilAttachmentRead |
      vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
    vk::ImageLayout::eDepthStencilAttachmentOptimal,
    vk::ImageAspectFlagBits::eDepth);
}

void GBuffer::prepareForRead(vk::CommandBuffer cmd_buf)
{
  etna::set_state(
    cmd_buf,
    albedo.get(),
    vk::PipelineStageFlagBits2::eFragmentShader,
    vk::AccessFlagBits2::eShaderSampledRead,
    vk::ImageLayout::eReadOnlyOptimal,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    normal.get(),
    vk::PipelineStageFlagBits2::eFragmentShader,
    vk::AccessFlagBits2::eShaderSampledRead,
    vk::ImageLayout::eReadOnlyOptimal,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    material.get(),
    vk::PipelineStageFlagBits2::eFragmentShader,
    vk::AccessFlagBits2::eShaderSampledRead,
    vk::ImageLayout::eReadOnlyOptimal,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    depth.get(),
    vk::PipelineStageFlagBits2::eFragmentShader,
    vk::AccessFlagBits2::eShaderSampledRead,
    vk::ImageLayout::eShaderReadOnlyOptimal,
    vk::ImageAspectFlagBits::eDepth);
}

std::vector<etna::RenderTargetState::AttachmentParams> GBuffer::genColorAttachmentParams(
  vk::AttachmentLoadOp load_op)
{
  // little bit ugly
  return {
    {.image = albedo.get(), .view = albedo.getView({}), .loadOp = load_op},
    {.image = normal.get(), .view = normal.getView({}), .loadOp = load_op},
    {.image = material.get(), .view = material.getView({}), .loadOp = load_op}};
}

etna::RenderTargetState::AttachmentParams GBuffer::genDepthAttachmentParams(
  vk::AttachmentLoadOp load_op, vk::AttachmentStoreOp store_op)
{
  return {.image = depth.get(), .view = depth.getView({}), .loadOp = load_op, .storeOp = store_op};
}

etna::RenderTargetState::AttachmentParams GBuffer::genShadowMappingAttachmentParams(
  vk::AttachmentLoadOp load_op, vk::AttachmentStoreOp store_op)
{
  return {
    .image = shadows.get(), .view = shadows.getView({}), .loadOp = load_op, .storeOp = store_op};
}

etna::Binding GBuffer::genAlbedoBinding(uint32_t index)
{
  return etna::Binding{
    index, albedo.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)};
}

etna::Binding GBuffer::genNormalBinding(uint32_t index)
{
  return etna::Binding{
    index, normal.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)};
}

etna::Binding GBuffer::genMaterialBinding(uint32_t index)
{
  return etna::Binding{
    index, material.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)};
}

etna::Binding GBuffer::genDepthBinding(uint32_t index)
{
  return etna::Binding{
    index, depth.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)};
}

etna::Binding GBuffer::genShadowBinding(uint32_t index)
{
  return etna::Binding{
    index, shadows.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)};
}
