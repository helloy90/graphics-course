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

  void allocateResources(vk::Extent3D spectrum_image_extent = {256, 256, 1});
  void loadShaders();
  void setupPipelines();
  void executeStart();
  void executeProgress(vk::CommandBuffer cmd_buf, float time);

  void drawGui();

  const etna::Image& getSpectrumImage() const { return updatedSpectrumImage; }
  // const etna::Image& getNormalMap() const { return terrainNormalMap; }
  const etna::Sampler& getSpectrumSampler() const { return spectrumSampler; }

private:
  void generateInitialSpectrum(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);
  void updateSpectrumForFFT(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, float time);

private:
  SpectrumGenerationParams params;
  etna::Buffer paramsBuffer;

  etna::Image initialSpectrumImage;
  etna::Image updatedSpectrumImage;
  etna::Image updatedSpectrumSlopeXImage;
  etna::Image updatedSpectrumSlopeZImage;
  etna::Image updatedSpectrumDisplacementXImage;
  etna::Image updatedSpectrumDisplacementZImage;

  etna::ComputePipeline initialSpectrumGenerationPipeline;
  etna::ComputePipeline spectrumProgressionPipeline;

  etna::Sampler spectrumSampler;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};
