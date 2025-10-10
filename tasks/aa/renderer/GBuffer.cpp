#include "GBuffer.hpp"

#include <vector>

#include <etna/DescriptorSet.hpp>
#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>


GBuffer::GBuffer(const CreateInfo& info)
{

  auto& ctx = etna::get_context();

  vk::Extent3D renderImagesExtent = {info.resolution.x, info.resolution.y, 1};
  vk::Extent3D shadowImagesExtent = {info.shadowMapsResolution.x, info.shadowMapsResolution.y, 1};

  albedo = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = renderImagesExtent,
      .name = "albedo",
      .format = info.renderTargetFormat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  normal = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = renderImagesExtent,
      .name = "normal",
      .format = info.normalsFormat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  material = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = renderImagesExtent,
      .name = "material",
      .format = vk::Format::eR8G8B8A8Unorm,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  depth = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = renderImagesExtent,
      .name = "depth",
      .format = info.depthFormat,
      .imageUsage =
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT});

  shadows.reserve(info.shadowCascadesAmount);

  for (uint32_t i = 0; i < info.shadowCascadesAmount; i++)
  {
    shadows.emplace_back(ctx.createImage(
      etna::Image::CreateInfo{
        .extent = shadowImagesExtent,
        .name = "shadows",
        .format = info.shadowsFormat,
        .imageUsage =
          vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled,
        .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .allocationCreate = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT}));
  }

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

  for (const auto& shadowMap : shadows)
  {
    etna::set_state(
      cmd_buf,
      shadowMap.get(),
      vk::PipelineStageFlagBits2::eEarlyFragmentTests |
        vk::PipelineStageFlagBits2::eLateFragmentTests,
      vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
      vk::ImageLayout::eDepthStencilAttachmentOptimal,
      vk::ImageAspectFlagBits::eDepth);
  }
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
    vk::AccessFlagBits2::eShaderStorageRead,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    normal.get(),
    vk::PipelineStageFlagBits2::eFragmentShader,
    vk::AccessFlagBits2::eShaderStorageRead,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    material.get(),
    vk::PipelineStageFlagBits2::eFragmentShader,
    vk::AccessFlagBits2::eShaderStorageRead,
    vk::ImageLayout::eGeneral,
    vk::ImageAspectFlagBits::eColor);
  etna::set_state(
    cmd_buf,
    depth.get(),
    vk::PipelineStageFlagBits2::eFragmentShader,
    vk::AccessFlagBits2::eShaderSampledRead,
    vk::ImageLayout::eShaderReadOnlyOptimal,
    vk::ImageAspectFlagBits::eDepth);

  for (const auto& shadowMap : shadows)
  {
    etna::set_state(
      cmd_buf,
      shadowMap.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eDepth);
  }
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
  uint32_t index, vk::AttachmentLoadOp load_op, vk::AttachmentStoreOp store_op)
{
  return {
    .image = shadows[index].get(),
    .view = shadows[index].getView({}),
    .loadOp = load_op,
    .storeOp = store_op};
}

etna::Binding GBuffer::genAlbedoBinding(uint32_t index)
{
  return etna::Binding{index, albedo.genBinding({}, vk::ImageLayout::eGeneral)};
}

etna::Binding GBuffer::genNormalBinding(uint32_t index)
{
  return etna::Binding{index, normal.genBinding({}, vk::ImageLayout::eGeneral)};
}

etna::Binding GBuffer::genMaterialBinding(uint32_t index)
{
  return etna::Binding{index, material.genBinding({}, vk::ImageLayout::eGeneral)};
}

etna::Binding GBuffer::genDepthBinding(uint32_t index)
{
  return etna::Binding{
    index, depth.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)};
}

std::vector<etna::Binding> GBuffer::genShadowBindings(uint32_t index)
{
  std::vector<etna::Binding> bindings;
  bindings.reserve(shadows.size());
  for (uint32_t i = 0; i < static_cast<uint32_t>(shadows.size()); i++)
  {
    bindings.emplace_back(
      etna::Binding{
        index, shadows[i].genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal), i});
  }

  return bindings;
}
