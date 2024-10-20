#include "App.hpp"

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <thread>

#include <stb_image.h>
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>

#include <tracy/Tracy.hpp>

App::App()
  : resolution{1280, 720}
  , useVsync{false}
{
  {
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};

    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    etna::initialize(etna::InitParams{
      .applicationName = "Local Shadertoy",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      // Replace with an index if etna detects your preferred GPU incorrectly
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = 1,
    });
  }


  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  {
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());

    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });

    resolution = {w, h};
  }

  commandManager = etna::get_context().createPerFrameCmdMgr();

  initShading();
}

void App::initShading()
{
  preparePrimitives();

  oneShotCommands = etna::get_context().createOneShotCmdMgr();

  loadTextures();

  loadCubemap();
}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  while (!osWindow->isBeingClosed())
  {
    ZoneScopedN("Frame");
    
    {
      ZoneScopedN("Poll OS Events");
      windowing.poll();
    }

    processInput();

    drawFrame();

    FrameMark;
  }

  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::processInput()
{
  auto& keyboard = osWindow->keyboard;
  if (keyboard[KeyboardKey::kB] == ButtonState::Falling)
  {
    const int retval = std::system("cd " GRAPHICS_COURSE_ROOT "/build"
                                   " && cmake --build . --target local_shadertoy_shaders");

    if (retval != 0)
      spdlog::warn("Shader recompilation returned a non-zero return code!");
    else
    {
      ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
      etna::reload_shaders();
      spdlog::info("Successfully reloaded shaders!");
    }
  }
}

void App::drawFrame()
{
  ZoneScoped;

  auto currentCmdBuf = commandManager->acquireNext();

  etna::begin_frame();

  auto nextSwapchainImage = vkWindow->acquireNext();

  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
      ETNA_PROFILE_GPU(currentCmdBuf, "Render Frame");

      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageAspectFlagBits::eColor);
      etna::flush_barriers(currentCmdBuf);

      etna::set_state(
        currentCmdBuf,
        generatedTexture.get(),
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(currentCmdBuf);

      // Begin generated texture render pass
      {
        ETNA_PROFILE_GPU(currentCmdBuf, "Texture Generation");
        etna::RenderTargetState state(
          currentCmdBuf,
          {{}, {resolution.x, resolution.y}},
          {{generatedTexture.get(), generatedTexture.getView({})}},
          {});

        currentCmdBuf.bindPipeline(
          vk::PipelineBindPoint::eGraphics, textureGenPipeline.getVkPipeline());


        double mouseX = 0;
        double mouseY = 0;
        glfwGetCursorPos(osWindow->native(), &mouseX, &mouseY);

        PushConstants pushConst{
          .iResolution = resolution,
          .mouseX = static_cast<float>(mouseX),
          .mouseY = static_cast<float>(mouseY)};

        currentCmdBuf.pushConstants<PushConstants>(
          textureGenPipeline.getVkPipelineLayout(),
          vk::ShaderStageFlagBits::eFragment,
          0,
          {pushConst});

        

        currentCmdBuf.draw(3, 1, 0, 0);
      }
      // End generated texture render pass

      std::this_thread::sleep_for(std::chrono::milliseconds(7)); //heavy work imitation

      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        vk::AccessFlagBits2::eColorAttachmentWrite,
        vk::ImageLayout::eColorAttachmentOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::set_state(
        currentCmdBuf,
        generatedTexture.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(currentCmdBuf);

      // Begin final image render pass
      {
        ETNA_PROFILE_GPU(currentCmdBuf, "SDF tracing");
        etna::RenderTargetState state{
          currentCmdBuf, {{}, {resolution.x, resolution.y}}, {{backbuffer, backbufferView}}, {}};

        auto graphicShaderInfo = etna::get_shader_program("graphic_shadertoy");

        auto set = etna::create_descriptor_set(
          graphicShaderInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {etna::Binding{
             0,
             generatedTexture.genBinding(
               textureSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{
             1,
             swordTexture.genBinding(
               textureSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{
             2,
             cubemapTexture.genBinding(
               cubemapSampler.get(),
               vk::ImageLayout::eShaderReadOnlyOptimal,
               {0,
                VK_REMAINING_MIP_LEVELS,
                0,
                VK_REMAINING_ARRAY_LAYERS,
                {},
                vk::ImageViewType::eCube})}});

        auto vkSet = set.getVkSet();
        currentCmdBuf.bindPipeline(
          vk::PipelineBindPoint::eGraphics, graphicsPipeline.getVkPipeline());
        currentCmdBuf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          graphicsPipeline.getVkPipelineLayout(),
          0,
          1,
          &vkSet,
          0,
          nullptr);

        double mouseX = 0;
        double mouseY = 0;
        glfwGetCursorPos(osWindow->native(), &mouseX, &mouseY);

        PushConstants pushConst{
          .iResolution = resolution,
          .mouseX = static_cast<float>(mouseX),
          .mouseY = static_cast<float>(mouseY)};

        currentCmdBuf.pushConstants<PushConstants>(
          graphicsPipeline.getVkPipelineLayout(),
          vk::ShaderStageFlagBits::eFragment,
          0,
          {pushConst});

        currentCmdBuf.draw(3, 1, 0, 0);
      }
      // End final image render pass

      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(currentCmdBuf);

      ETNA_READ_BACK_GPU_PROFILING(currentCmdBuf);
    }
    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    auto renderingDone =
      commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();

  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}

// Shading initialization details

void App::preparePrimitives()
{

  etna::create_program(
    "graphic_shadertoy",
    {INFLIGHT_FRAMES_SHADERS_ROOT "main_shader.frag.spv",
     INFLIGHT_FRAMES_SHADERS_ROOT "decoy.vert.spv"});
  etna::create_program(
    "texture_generation",
    {INFLIGHT_FRAMES_SHADERS_ROOT "texture_gen.frag.spv",
     INFLIGHT_FRAMES_SHADERS_ROOT "decoy.vert.spv"});


  graphicsPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "graphic_shadertoy",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb},
      }});

  textureGenPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "texture_generation",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb},
      }});

  generatedTexture = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "generated_texture_image",
    .format = vk::Format::eB8G8R8A8Srgb,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});

  textureSampler =
    etna::Sampler({.addressMode = vk::SamplerAddressMode::eRepeat, .name = "sampler"});
  cubemapSampler = etna::Sampler(
    {.filter = vk::Filter::eLinear,
     .addressMode = vk::SamplerAddressMode::eRepeat,
     .name = "sampler"});

    transferHelper =
    std::make_unique<etna::BlockingTransferHelper>(etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = resolution.x * resolution.y * 4 * 6,
    });
}

void App::loadTextures()
{
  uint32_t layerCount = 1;
  char swordTextureFilename[] = INFLIGHT_FRAMES_TEXTURES "test_tex_1.png";
  int width, height, channels;
  unsigned char* swordTextureData =
    stbi_load(swordTextureFilename, &width, &height, &channels, STBI_rgb_alpha);

  if (swordTextureData == nullptr)
  {
    ETNA_PANIC("Texture {} is not loaded!", swordTextureFilename);
  }

  uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

  const vk::DeviceSize swordTextureSize = width * height * 4;

  etna::Buffer swordTextureBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = swordTextureSize,
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc |
      vk::BufferUsageFlagBits::eTransferDst,
    .name = "sword_texture_buffer",
  });

  auto source = std::span<unsigned char>(swordTextureData, swordTextureSize);
  transferHelper->uploadBuffer(*oneShotCommands, swordTextureBuffer, 0, std::as_bytes(source));

  swordTexture = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
    .name = "sword_texture_image",
    .format = vk::Format::eR8G8B8A8Srgb,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
      vk::ImageUsageFlagBits::eTransferSrc,
    .mipLevels = mipLevels});

  localCopyBufferToImage(swordTextureBuffer, swordTexture, layerCount);

  generateMipmaps(swordTexture, mipLevels, layerCount);

  stbi_image_free(swordTextureData);
}

void App::loadCubemap()
{
  const uint32_t layerCount = 6;
  std::string path = INFLIGHT_FRAMES_TEXTURES "Cubemaps/Moonlight/";
  std::vector<std::string> filenames = {
    path + "moonlight_front.bmp",
    path + "moonlight_back.bmp",
    path + "moonlight_up.bmp",
    path + "moonlight_down.bmp",
    path + "moonlight_left.bmp",
    path + "moonlight_right.bmp",
  };

  if (filenames.size() != layerCount)
  {
    ETNA_PANIC("Amount of textures is not equal to amount of image layers!");
  }

  unsigned char* textures[layerCount];
  int width, height, channels;
  for (uint32_t i = 0; i < layerCount; i++)
  {
    textures[i] = stbi_load(filenames[i].c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (textures[i] == nullptr)
    {
      ETNA_PANIC("Texture {} is not loaded!", filenames[i].c_str());
    }
  }

  uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

  const vk::DeviceSize cubemapSize = width * height * 4 * layerCount;
  const vk::DeviceSize layerSize = cubemapSize / layerCount;

  etna::Buffer cubemapBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = cubemapSize,
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer |
      vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferSrc |
      vk::BufferUsageFlagBits::eTransferDst,
    .name = "cubemap_buffer",
  });

  for (uint32_t i = 0; i < layerCount; i++)
  {
    auto source = std::span<unsigned char>(textures[i], layerSize);
    transferHelper->uploadBuffer(
      *oneShotCommands, cubemapBuffer, static_cast<uint32_t>(layerSize * i), std::as_bytes(source));
  }

  cubemapTexture = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
    .name = "cubemap_image",
    .format = vk::Format::eR8G8B8A8Srgb,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
      vk::ImageUsageFlagBits::eTransferSrc,
    .layers = layerCount,
    .mipLevels = mipLevels,
    .flags = vk::ImageCreateFlagBits::eCubeCompatible});

  localCopyBufferToImage(cubemapBuffer, cubemapTexture, layerCount);

  generateMipmaps(cubemapTexture, mipLevels, layerCount);

  for (int i = 0; i < 6; i++)
  {
    stbi_image_free(textures[i]);
  }
}


void App::localCopyBufferToImage(
  const etna::Buffer& buffer, const etna::Image& image, uint32_t layer_count)
{

  auto commandBuffer = oneShotCommands->start();

  auto extent = image.getExtent();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    etna::set_state(
      commandBuffer,
      image.get(),
      vk::PipelineStageFlagBits2::eTransfer,
      vk::AccessFlagBits2::eTransferWrite,
      vk::ImageLayout::eTransferDstOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    vk::BufferImageCopy copyRegion = {
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource =
        {.aspectMask = vk::ImageAspectFlagBits::eColor,
         .mipLevel = 0,
         .baseArrayLayer = 0,
         .layerCount = layer_count},
      .imageExtent =
        vk::Extent3D{static_cast<uint32_t>(extent.width), static_cast<uint32_t>(extent.height), 1}};

    commandBuffer.copyBufferToImage(
      buffer.get(), image.get(), vk::ImageLayout::eTransferDstOptimal, 1, &copyRegion);

    etna::set_state(
      commandBuffer,
      image.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void App::generateMipmaps(const etna::Image& image, uint32_t mip_levels, uint32_t layer_count)
{
  auto extent = image.getExtent();

  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {

    int32_t mipWidth = extent.width;
    int32_t mipHeight = extent.height;

    for (uint32_t i = 1; i < mip_levels; i++)
    {
      etna::set_state(
        commandBuffer,
        image.get(),
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        vk::ImageLayout::eGeneral,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(commandBuffer);

      std::array srcOffset = {
        vk::Offset3D{}, vk::Offset3D{mipWidth, mipHeight, 1}};
      auto srdImageSubrecourceLayers = vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = i - 1,
        .baseArrayLayer = 0,
        .layerCount = layer_count};
      std::array dstOffset = {
        vk::Offset3D{},
        vk::Offset3D{mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1}};
      auto dstImageSubrecourceLayers = vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = i,
        .baseArrayLayer = 0,
        .layerCount = layer_count};

      auto imageBlit = vk::ImageBlit2{
        .sType = vk::StructureType::eImageBlit2,
        .pNext = nullptr,
        .srcSubresource = srdImageSubrecourceLayers,
        .srcOffsets = srcOffset,
        .dstSubresource = dstImageSubrecourceLayers,
        .dstOffsets = dstOffset};

      auto blitInfo = vk::BlitImageInfo2{
        .sType = vk::StructureType::eBlitImageInfo2,
        .pNext = nullptr,
        .srcImage = image.get(),
        .srcImageLayout = vk::ImageLayout::eGeneral,
        .dstImage = image.get(),
        .dstImageLayout = vk::ImageLayout::eGeneral,
        .regionCount = 1,
        .pRegions = &imageBlit,
        .filter = vk::Filter::eLinear};

      commandBuffer.blitImage2(&blitInfo);

      etna::set_state(
        commandBuffer,
        image.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(commandBuffer);

      if (mipWidth > 1)
      {
        mipWidth /= 2;
      }
      if (mipHeight > 1)
      {
        mipHeight /= 2;
      }
    }

    etna::set_state(
      commandBuffer,
      image.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);
  }

  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}
