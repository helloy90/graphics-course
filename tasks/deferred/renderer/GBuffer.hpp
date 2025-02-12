#pragma once

#include "etna/DescriptorSet.hpp"
#include "etna/RenderTargetStates.hpp"
#include <array>

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

  etna::RenderTargetState::AttachmentParams genDepthAttachemtParams();

  std::vector<etna::Binding> genBindings();

private:
  std::array<etna::Image, 3> colorImages; // size hardcode for now
  etna::Image depth;
};
