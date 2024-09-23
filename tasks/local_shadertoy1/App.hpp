#pragma once

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Image.hpp>

#include "etna/Sampler.hpp"
#include "render_utils/shaders/cpp_glsl_compat.h"

#include "wsi/OsWindowingManager.hpp"


class App
{
public:
  App();
  ~App();

  void run();

  struct PushConstants {
    shader_uvec2 iResolution;
    float mouseX;
    float mouseY;
  } pushConst;

private:
  void drawFrame();

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  // -----------------

  etna::ComputePipeline compPipeline;
  etna::Image storage;
  
  etna::Sampler sampler;

  void initComputeSystems();
  // -----------------

  glm::uvec2 resolution;
  bool useVsync;

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;
};
