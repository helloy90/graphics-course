#pragma once

#include <glm/glm.hpp>

#include <etna/Buffer.hpp>
#include <etna/Image.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Sampler.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <etna/BlockingTransferHelper.hpp>


class SSAOModule
{
public:
  struct AllocationInfo
  {
    glm::uvec2 noiseTextureSize;
    uint32_t kernelSize;
    uint32_t seed;
  };

public:
  SSAOModule();

  void allocateResources(const AllocationInfo& info);
  void loadShaders();
  void setupPipelines();

  void execute(
    vk::CommandBuffer cmd_buf,
    const etna::Image& normal_texture,
    const etna::Image& depth_texture,
    const etna::Sampler& depth_sampler,
    const etna::Image& occlusion_texture);

private:
  etna::Image noiseTexture;
  etna::Buffer ssaoKernel;
  uint32_t kernelSize;

  etna::Sampler noiseSampler;

  etna::ComputePipeline ssaoPipeline;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;
};
