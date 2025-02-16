#include "Renderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>

#include <imgui.h>


Renderer::Renderer(glm::uvec2 res)
  : resolution{res}
{
}

void Renderer::initVulkan(std::span<const char*> instance_extensions)
{
  std::vector<const char*> instanceExtensions;

  for (auto ext : instance_extensions)
    instanceExtensions.push_back(ext);

  std::vector<const char*> deviceExtensions;

  deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  etna::initialize(etna::InitParams{
    .applicationName = "deferred_renderer",
    .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
    .instanceExtensions = instanceExtensions,
    .deviceExtensions = deviceExtensions,
    .features =
      vk::PhysicalDeviceFeatures2{
        .features =
          {.tessellationShader = vk::True,
           .fillModeNonSolid = vk::True /*debug*/,
           .fragmentStoresAndAtomics = vk::True}},
    .physicalDeviceIndexOverride = {},
    .numFramesInFlight = 2,
  });
}

void Renderer::initFrameDelivery(vk::UniqueSurfaceKHR a_surface, ResolutionProvider res_provider)
{
  resolutionProvider = std::move(res_provider);

  auto& ctx = etna::get_context();

  commandManager = ctx.createPerFrameCmdMgr();

  window = ctx.createWindow(etna::Window::CreateInfo{
    .surface = std::move(a_surface),
  });

  auto [w, h] = window->recreateSwapchain(etna::Window::DesiredProperties{
    .resolution = {resolution.x, resolution.y},
    .vsync = useVsync,
  });

  resolution = {w, h};

  worldRenderer = std::make_unique<WorldRenderer>();

  guiRenderer = std::make_unique<ImGuiRenderer>(window->getCurrentFormat());

  worldRenderer->allocateResources(resolution);
  worldRenderer->loadShaders();
  worldRenderer->loadLights();
  worldRenderer->setupRenderPipelines();
  worldRenderer->setupTerrainGeneration(vk::Format::eR32Sfloat, {4096, 4096, 1});
  worldRenderer->generateTerrain();
}

void Renderer::loadScene(std::filesystem::path path)
{
  worldRenderer->loadScene(path);
}

void Renderer::debugInput(const Keyboard& kb)
{
  if (kb[KeyboardKey::kB] == ButtonState::Falling)
  {
    reloadShaders();
  }

  worldRenderer->debugInput(kb);
}

void Renderer::update(const FramePacket& packet)
{
  worldRenderer->update(packet);
}

void Renderer::drawGui()
{
  ImGui::Begin("Render Settings");

  if (ImGui::CollapsingHeader("Application Settings"))
  {
    if (ImGui::Checkbox("Use Vsync", &useVsync))
    {
      swapchainRecreationNeeded = true;
    }
  }

  if (ImGui::Button("Reload shaders"))
  {
    reloadShaders();
  }

  ImGui::End();
}

void Renderer::drawFrame()
{
  ZoneScoped;

  {
    ZoneScopedN("drawGui");
    guiRenderer->nextFrame();
    ImGui::NewFrame();
    worldRenderer->drawGui();
    drawGui();
    ImGui::Render();
  }

  auto currentCmdBuf = commandManager->acquireNext();

  etna::begin_frame();

  auto nextSwapchainImage = window->acquireNext();

  if (nextSwapchainImage)
  {
    auto [image, view, availableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
      ETNA_PROFILE_GPU(currentCmdBuf, renderFrame);

      worldRenderer->renderWorld(currentCmdBuf, image); // view);

      etna::set_state(
        currentCmdBuf,
        image,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentRead,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);

      {
        ImDrawData* pDrawData = ImGui::GetDrawData();
        guiRenderer->render(
          currentCmdBuf, {{0, 0}, {resolution.x, resolution.y}}, image, view, pDrawData);
      }

      etna::set_state(
        currentCmdBuf,
        image,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(currentCmdBuf);

      ETNA_READ_BACK_GPU_PROFILING(currentCmdBuf);
    }
    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    auto renderingDone = commandManager->submit(std::move(currentCmdBuf), std::move(availableSem));

    const bool presented = window->present(std::move(renderingDone), view);

    if (!presented)
      nextSwapchainImage = std::nullopt;

    if (swapchainRecreationNeeded)
    {
      nextSwapchainImage = std::nullopt;
      swapchainRecreationNeeded = false;
    }
  }

  if (!nextSwapchainImage && resolutionProvider() != glm::uvec2{0, 0})
  {
    spdlog::info("recreating swapchain");
    auto [w, h] = window->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }

  etna::end_frame();
}

void Renderer::reloadShaders()
{
  const int retval = std::system("cd " GRAPHICS_COURSE_ROOT "/build"
                                 " && cmake --build . --target deferred_renderer_shaders");
  if (retval != 0)
    spdlog::warn("Shader recompilation returned a non-zero return code!");
  else
  {
    ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
    etna::reload_shaders();
    spdlog::info("Successfully reloaded shaders!");
  }
}

Renderer::~Renderer()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}
