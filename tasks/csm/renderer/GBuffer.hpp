#pragma once

#include "etna/DescriptorSet.hpp"
#include "etna/RenderTargetStates.hpp"
#include "etna/Sampler.hpp"

#include <etna/Image.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan_enums.hpp>

class GBuffer
{
public:
  struct CreateInfo
  {
    glm::uvec2 resolution;
    glm::uvec2 shadowMapsResolution;
    vk::Format renderTargetFormat;
    vk::Format normalsFormat;
    vk::Format shadowsFormat;
    uint32_t shadowCascadesAmount;
  };

public:
  explicit GBuffer(const CreateInfo& info);

  // no flush
  void prepareForRender(vk::CommandBuffer cmd_buf);

  // no flush
  void continueDepthWrite(vk::CommandBuffer cmd_buf);

  // no flush
  void prepareForRead(vk::CommandBuffer cmd_buf);

  std::vector<etna::RenderTargetState::AttachmentParams> genColorAttachmentParams(
    vk::AttachmentLoadOp load_op = vk::AttachmentLoadOp::eClear);

  etna::RenderTargetState::AttachmentParams genDepthAttachmentParams(
    vk::AttachmentLoadOp load_op = vk::AttachmentLoadOp::eClear,
    vk::AttachmentStoreOp store_op = vk::AttachmentStoreOp::eStore);

  etna::RenderTargetState::AttachmentParams genShadowMappingAttachmentParams(
    uint32_t index,
    vk::AttachmentLoadOp load_op = vk::AttachmentLoadOp::eClear,
    vk::AttachmentStoreOp store_op = vk::AttachmentStoreOp::eStore);

  etna::Binding genAlbedoBinding(uint32_t index);
  etna::Binding genNormalBinding(uint32_t index);
  etna::Binding genMaterialBinding(uint32_t index);
  etna::Binding genDepthBinding(uint32_t index);
  std::vector<etna::Binding> genShadowBindings(uint32_t index);

  vk::Extent2D getShadowTextureExtent() const
  {
    const auto& extent = shadows[0].getExtent();
    return {extent.width, extent.height};
  }

  vk::Format getShadowTextureFormat() const { return shadows[0].getFormat(); }

private:
  etna::Image albedo;
  etna::Image normal;
  etna::Image material;
  etna::Image depth;
  std::vector<etna::Image> shadows;

  etna::Sampler sampler;
};
