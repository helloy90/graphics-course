#pragma once

#include <glm/glm.hpp>

#include <etna/Image.hpp>
#include <etna/Buffer.hpp>
#include <etna/GpuSharedResource.hpp>
#include <etna/ComputePipeline.hpp>

#include "etna/Sampler.hpp"
#include "shaders/AntialiasingParams.h"


class AntialiasingModule
{
public:
  struct AllocationInfo
  {
    glm::uvec2 resolution;
    vk::Format renderTargetFormat;
    vk::Format depthFormat;
  };

public:
  explicit AntialiasingModule();

  void allocateResources(const AllocationInfo& info);
  void loadShaders();
  void setupPipelines();

  void setBarriersForCopy(vk::CommandBuffer cmd_buf);
  void setBarriersForExecute(vk::CommandBuffer cmd_buf);

  void updateJitter();

  void execute(
    vk::CommandBuffer cmd_buf,
    const etna::Image& render_target,
    const etna::Image& depth_image,
    const glm::mat4& proj_view,
    const glm::mat4& inv_proj_view);

  // Should be the last operation before copying to swapchain in render function.
  // No barriers are set inside this function so all appropriate barriers should be set before and
  // after the call
  void copyPreviousData(
    vk::CommandBuffer cmd_buf,
    const etna::Image& prev_target,
    const etna::Image& prev_depth_image,
    const glm::mat4& previous_proj_view);

  void drawGui();

  const glm::vec2& getCurrentCameraJitter() const { return currentJitter; };
  const glm::vec2& getPreviousCameraJitter() const { return previousJitter; };
  const glm::mat4& getPreviousProjView() const { return params.previousProjView; }

private:
  static float haltonJitter(uint32_t index, uint32_t base);

private:
  etna::Image previousTargetImage;
  etna::Image previousDepthImage;

  etna::Sampler depthSampler;

  AntialiasingParams params;
  std::optional<etna::GpuSharedResource<etna::Buffer>> paramsBuffer;

  etna::ComputePipeline aaPipeline;

  uint32_t jitterIndex;

  glm::vec2 currentJitter;
  glm::vec2 previousJitter;

  bool enable = false;
};
