#pragma once

#include <glm/glm.hpp>

#include <etna/Image.hpp>


class AntialiasingModule
{
public:
  struct CreateInfo
  {
    glm::uvec2 resolution;
    vk::Format renderTargetFormat;
    vk::Format depthFormat;
  };

public:
  explicit AntialiasingModule(const CreateInfo& info);

  void getPreviousImages(
    vk::CommandBuffer cmd_buf, const etna::Image& prev_target, const etna::Image& prev_depth);
  void execute(vk::CommandBuffer cmd_buf, const etna::Image& render_target);

private:
  etna::Image previousTargetImage;
  etna::Image previousDepthImage;

  bool enable = false;
};
