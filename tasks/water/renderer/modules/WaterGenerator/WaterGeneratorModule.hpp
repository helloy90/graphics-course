#pragma once

#include <etna/Buffer.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <etna/Sampler.hpp>

#include "shaders/SpectrumGenerationParams.h"


class WaterGeneratorModule
{
public:
  WaterGeneratorModule();

  void allocateResources(
    vk::Format spectrum_image_format = vk::Format::eR32Sfloat,
    vk::Extent3D spectrum_image_extent = {256, 256, 1});
  void loadShaders();
  void setupPipelines();
  void execute();

private:
  void generateSpectrum(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);

private:
  SpectrumGenerationParams params;
  etna::Buffer paramsBuffer;

  etna::Image spectrumImage;

  etna::ComputePipeline spectrumGenerationPipeline;

  etna::Sampler spectrumSampler;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};
