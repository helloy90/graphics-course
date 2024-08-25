#pragma once

#include <etna/GlobalContext.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <glm/glm.hpp>
#include <function2/function2.hpp>

#include "wsi/Keyboard.hpp"

#include "FramePacket.hpp"
#include "WorldRenderer.hpp"


class Renderer
{
public:
  explicit Renderer(glm::uvec2 resolution);
  ~Renderer();

  void initVulkan(std::span<const char*> instance_extensions);
  void initFrameDelivery(vk::UniqueSurfaceKHR surface);
  void recreateSwapchain(glm::uvec2 res);
  void loadScene(std::filesystem::path path);

  void debugInput(const Keyboard& kb);
  void update(const FramePacket& packet);
  void drawFrame();

private:
  std::unique_ptr<etna::Window> window;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

  glm::uvec2 resolution;
  bool useVsync = true;

  std::unique_ptr<WorldRenderer> worldRenderer;
};