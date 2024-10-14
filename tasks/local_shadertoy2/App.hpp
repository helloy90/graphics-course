#pragma once

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>

#include "etna/BlockingTransferHelper.hpp"
#include "etna/GraphicsPipeline.hpp"
#include "etna/OneShotCmdMgr.hpp"
#include "etna/Sampler.hpp"
#include "render_utils/shaders/cpp_glsl_compat.h"

#include "wsi/OsWindowingManager.hpp"


class App
{
public:
  App();
  ~App();

  void run();

  struct PushConstants
  {
    shader_uvec2 iResolution;
    float mouseX;
    float mouseY;
  } pushConst;

private:
  void drawFrame();

  void processInput();

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  // -----------------
  etna::GraphicsPipeline graphicsPipeline;
  etna::GraphicsPipeline textureGenPipeline;

  etna::Sampler textureSampler;
  etna::Sampler cubemapSampler;
  etna::Image generatedTexture;

  etna::Image swordTexture;

  etna::Image cubemapTexture;
  
  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;

  void preparePrimitives();
  void loadTextures();
  void loadCubemap();
  void initShading();

  void localCopyBufferToImage(const etna::Buffer& buffer, const etna::Image& image, uint32_t layer_count);
  void generateMipmaps(const etna::Image& image, uint32_t mip_levels, uint32_t layer_count);
  // -----------------

  glm::uvec2 resolution;
  bool useVsync;

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;
};
