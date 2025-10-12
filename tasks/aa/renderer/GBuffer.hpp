#pragma once

#include <glm/glm.hpp>

#include <etna/DescriptorSet.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Sampler.hpp>
#include <etna/Image.hpp>
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
    vk::Format depthFormat;
    vk::Format shadowsFormat;
    uint32_t shadowCascadesAmount;
  };

public:
  explicit GBuffer(const CreateInfo& info);

  // no flush
  void prepareForRender(vk::CommandBuffer cmd_buf);

  // used for transparent depth read/write
  // no flush
  void prepareForDepthReadWrite(vk::CommandBuffer cmd_buf);

  void prepareForDepthRead(vk::CommandBuffer cmd_buf, vk::PipelineStageFlagBits2 pipeline_stage);

  // no flush
  void prepareForRead(vk::CommandBuffer cmd_buf);

  // used to set barrier for depth image for taa copy
  // no flush
  void prepareForDepthCopy(vk::CommandBuffer cmd_buf);

  std::vector<etna::RenderTargetState::AttachmentParams> genColorAttachmentParams(
    vk::AttachmentLoadOp load_op = vk::AttachmentLoadOp::eClear);

  etna::RenderTargetState::AttachmentParams genDepthAttachmentParams(
    vk::AttachmentLoadOp load_op = vk::AttachmentLoadOp::eClear,
    vk::AttachmentStoreOp store_op = vk::AttachmentStoreOp::eStore);

  etna::RenderTargetState::AttachmentParams genShadowMappingAttachmentParams(
    uint32_t index,
    vk::AttachmentLoadOp load_op = vk::AttachmentLoadOp::eClear,
    vk::AttachmentStoreOp store_op = vk::AttachmentStoreOp::eStore);

  etna::Binding genAlbedoBinding(uint32_t index, vk::ImageLayout layout);
  etna::Binding genNormalBinding(uint32_t index, vk::ImageLayout layout);
  etna::Binding genMaterialBinding(uint32_t index, vk::ImageLayout layout);
  etna::Binding genDepthBinding(uint32_t index, vk::ImageLayout layout);
  std::vector<etna::Binding> genShadowBindings(uint32_t index, vk::ImageLayout layout);

  vk::Extent2D getShadowTextureExtent() const
  {
    const auto& extent = shadows[0].getExtent();
    return {extent.width, extent.height};
  }

  vk::Format getShadowTextureFormat() const { return shadows[0].getFormat(); }

  const etna::Image& getDepthImage() const { return depth; }

private:
  etna::Image albedo;
  etna::Image normal;
  etna::Image material;
  etna::Image depth;
  std::vector<etna::Image> shadows;

  etna::Sampler sampler;
};
