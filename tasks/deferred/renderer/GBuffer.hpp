#pragma once

#include "etna/DescriptorSet.hpp"
#include "etna/RenderTargetStates.hpp"
#include "etna/Sampler.hpp"

#include <etna/Image.hpp>
#include <glm/glm.hpp>

class GBuffer
{
public:
  explicit GBuffer(glm::uvec2 resolution, vk::Format render_target_format);

  // no flush
  void prepareForRender(vk::CommandBuffer cmd_buf);

  // no flush
  void prepareForRead(vk::CommandBuffer cmd_buf);

  std::vector<etna::RenderTargetState::AttachmentParams> genColorAttachmentParams();

  etna::RenderTargetState::AttachmentParams genDepthAttachmentParams();

  etna::Binding genAlbedoBinding(uint32_t index);
  etna::Binding genNormalBinding(uint32_t index);
  etna::Binding genDepthBinding(uint32_t index);

private:
  etna::Image albedo;
  etna::Image normal;
  etna::Image depth;

  etna::Sampler sampler;
};
