#pragma once

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>

#include <etna/BlockingTransferHelper.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/OneShotCmdMgr.hpp>
#include <etna/Sampler.hpp>

#include "wsi/OsWindowingManager.hpp"

#include "shaders/UniformParams.h"

class App
{
public:
  App();
  ~App();

  void run();

private:
  void drawFrame();

  void updateUniformParams(etna::Buffer& params);

  void processInput();

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  // -----------------
  uint32_t numFramesInFlight;

  UniformParams uniformParams;

  etna::GraphicsPipeline graphicsPipeline;
  etna::GraphicsPipeline textureGenPipeline;

  etna::Sampler textureSampler;
  etna::Sampler cubemapSampler;

  std::optional<etna::GpuSharedResource<etna::Buffer>> constants;

  etna::Image generatedTexture;
  etna::Image swordTexture;
  etna::Image cubemapTexture;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;

  glm::uvec2 maxTextureResolution;

  void preparePrimitives();
  void loadTextures();
  void loadCubemap();
  void initShading();

  void localCopyBufferToImage(
    const etna::Buffer& buffer, const etna::Image& image, uint32_t layer_count);
  void generateMipmaps(const etna::Image& image, uint32_t mip_levels, uint32_t layer_count);
  // -----------------

  glm::uvec2 resolution;
  bool useVsync;

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;
};
