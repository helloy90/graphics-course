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

  void allocateResources(uint32_t textures_extent = 256);
  void loadShaders();
  void setupPipelines();
  void executeStart();
  void executeProgress(vk::CommandBuffer cmd_buf, float time);

  void drawGui();

  const etna::Image& getSpectrumImage() const { return updatedSpectrumTexture; }
  // const etna::Image& getNormalMap() const { return terrainNormalMap; }
  const etna::Sampler& getSpectrumSampler() const { return spectrumSampler; }

private:
  void generateInitialSpectrum(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);
  void precomputeTwiddleFactors(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);
  
  void updateSpectrumForFFT(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, float time);

private:
  SpectrumGenerationParams params;
  etna::Buffer paramsBuffer;

  etna::Image initialSpectrumTexture;
  etna::Image updatedSpectrumTexture;
  etna::Image updatedSpectrumSlopeXTexture;
  etna::Image updatedSpectrumSlopeZTexture;
  etna::Image updatedSpectrumDisplacementXTexture;
  etna::Image updatedSpectrumDisplacementZTexture;

  etna::Image twiddleFactorsTexture;

  etna::ComputePipeline initialSpectrumGenerationPipeline;
  etna::ComputePipeline twiddleFactorsPrecomputePipeline;

  etna::ComputePipeline spectrumProgressionPipeline;

  etna::Sampler spectrumSampler;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};
