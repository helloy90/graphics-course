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

  const etna::Image& getHeightMap() const { return heightMap; }
  const etna::Image& getNormalMap() const { return normalMap; }
  const etna::Sampler& getSampler() const { return textureSampler; }

private:
  struct InverseFFTInfo
  {
    uint32_t size;
    uint32_t logSize;
    uint32_t texturesAmount;
  };

private:
  void generateInitialSpectrum(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);

  void updateSpectrumForFFT(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, float time);

  void inverseFFT(vk::CommandBuffer cmd_buf);

  void executeInverseFFT(
    vk::CommandBuffer cmd_buf,
    vk::PipelineLayout pipeline_layout,
    const char* shader_program,
    etna::PersistentDescriptorSet persistent_set,
    vk::Extent3D extent);

  void assembleMaps(vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout);

private:
  SpectrumGenerationParams params;
  etna::Buffer paramsBuffer;

  InverseFFTInfo info;
  etna::Buffer infoBuffer;

  etna::Image initialSpectrumTexture;

  etna::Image updatedSpectrumTexture;
  etna::Image updatedSpectrumSlopeTexture;
  etna::Image updatedSpectrumDisplacementTexture;

  etna::Image heightMap;
  etna::Image normalMap;

  std::optional<etna::PersistentDescriptorSet> horizontalInverseFFTDescriptorSet;
  std::optional<etna::PersistentDescriptorSet> verticalIInverseFFTDescriptorSet;

  etna::ComputePipeline initialSpectrumGenerationPipeline;

  etna::ComputePipeline spectrumProgressionPipeline;
  etna::ComputePipeline horizontalInverseFFTPipeline;
  etna::ComputePipeline verticalInverseFFTPipeline;

  etna::ComputePipeline assemblerPipeline;

  etna::Sampler textureSampler;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
};
