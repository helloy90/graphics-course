#include "App.hpp"
#include "GLFW/glfw3.h"

#include <algorithm>
#include <cstdint>
#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <vulkan/vulkan.hpp>

#include <stb_image.h>

App::App()
  : maxTextureResolution{3840, 2160}
  , resolution{1280, 720}
  , useVsync{true}
{
  // First, we need to initialize Vulkan, which is not trivial because
  // extensions are required for just about anything.
  {
    // GLFW tells us which extensions it needs to present frames to the OS
    // window. Actually rendering anything to a screen is optional in Vulkan,
    // you can alternatively save rendered frames into files, send them over
    // network, etc. Instance extensions do not depend on the actual GPU, only
    // on the OS.
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};

    // We also need the swapchain device extension to get access to the OS
    // window from inside of Vulkan on the GPU.
    // Device extensions require HW support from the GPU.
    // Generally, in Vulkan, we call the GPU a "device" and the CPU/OS
    // combination a "host."
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // Etna does all of the Vulkan initialization heavy lifting.
    // You can skip figuring out how it works for now.
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

  // Now we can create an OS window
  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  // But we also need to hook the OS window up to Vulkan manually!
  {
    // First, we ask GLFW to provide a "surface" for the window,
    // which is an opaque description of the area where we can actually render.
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());

    // Then we pass it to Etna to do the complicated work for us
    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    // And finally ask Etna to create the actual swapchain so that we can
    // get (different) images each frame to render stuff into.
    // Here, we do not support window resizing, so we only need to call this
    // once.
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });

    // Technically, Vulkan might fail to initialize a swapchain with the
    // requested resolution and pick a different one. This, however, does not
    // occur on platforms we support. Still, it's better to follow the
    // "intended" path.
    resolution = {w, h};
  }

  // Next, we need a magical Etna helper to send commands to the GPU.
  // How it is actually performed is not trivial, but we can skip this for now.
  commandManager = etna::get_context().createPerFrameCmdMgr();

  // TODO: Initialize any additional resources you require here!

  initShading();
}

void App::initShading()
{
  preparePrimitives();

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
    windowing.poll();

    processInput();

    drawFrame();
  }

  // We need to wait for the GPU to execute the last frame before destroying
  // all resources and closing the application.
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
  // First, get a command buffer to write GPU commands into.
  auto currentCmdBuf = commandManager->acquireNext();

  // Next, tell Etna that we are going to start processing the next frame.
  etna::begin_frame();

  // And now get the image we should be rendering the picture into.
  auto nextSwapchainImage = vkWindow->acquireNext();

  // When window is minimized, we can't render anything in Windows
  // because it kills the swapchain, so we skip frames in this case.
  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
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
        etna::RenderTargetState state(
          currentCmdBuf,
          {{}, {resolution.x, resolution.y}},
          {{generatedTexture.get(), generatedTexture.getView({})}},
          {});

        currentCmdBuf.bindPipeline(
          vk::PipelineBindPoint::eGraphics, textureGenPipeline.getVkPipeline());

        // Getting mouse coords
        double mouseX = 0;
        double mouseY = 0;
        glfwGetCursorPos(osWindow->native(), &mouseX, &mouseY);

        pushConst.iResolution = resolution;
        pushConst.mouseX = static_cast<float>(mouseX);
        pushConst.mouseY = static_cast<float>(mouseY);

        currentCmdBuf.pushConstants<PushConstants>(
          textureGenPipeline.getVkPipelineLayout(),
          vk::ShaderStageFlagBits::eFragment,
          0,
          {pushConst});

        currentCmdBuf.draw(3, 1, 0, 0);
      }
      // End generated texture render pass

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

      etna::set_state(
        currentCmdBuf,
        swordTexture.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::set_state(
        currentCmdBuf,
        cubemapTexture.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(currentCmdBuf);

      // Begin final image render pass
      {
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
               {.type = vk::ImageViewType::eCube})}});

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

        // Getting mouse coords
        double mouseX = 0;
        double mouseY = 0;
        glfwGetCursorPos(osWindow->native(), &mouseX, &mouseY);

        pushConst.iResolution = resolution;
        pushConst.mouseX = static_cast<float>(mouseX);
        pushConst.mouseY = static_cast<float>(mouseY);

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
    }
    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    // We are done recording GPU commands now and we can send them to be
    // executed by the GPU. Note that the GPU won't start executing our commands
    // before the semaphore is signalled, which will happen when the OS says
    // that the next swapchain image is ready.
    auto renderingDone =
      commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    // Finally, present the backbuffer the screen, but only after the GPU tells
    // the OS that it is done executing the command buffer via the renderingDone
    // semaphore.
    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();

  // After a window us un-minimized, we need to restore the swapchain to
  // continue rendering.
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
  // Create shader program
  etna::create_program(
    "graphic_shadertoy",
    {LOCAL_SHADERTOY2_SHADERS_ROOT "main_shader.frag.spv",
     LOCAL_SHADERTOY2_SHADERS_ROOT "decoy.vert.spv"});
  etna::create_program(
    "texture_generation",
    {LOCAL_SHADERTOY2_SHADERS_ROOT "texture_gen.frag.spv",
     LOCAL_SHADERTOY2_SHADERS_ROOT "decoy.vert.spv"});

  // Create pipeline
  graphicsPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "graphic_shadertoy",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {vkWindow->getCurrentFormat()},
      }});

  textureGenPipeline = etna::get_context().getPipelineManager().createGraphicsPipeline(
    "texture_generation",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {vk::Format::eB8G8R8A8Srgb},
      }});

  // Create generated texture image
  generatedTexture = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "generated_texture_image",
    .format = vk::Format::eB8G8R8A8Srgb,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});

  // Create samplers

  textureSampler =
    etna::Sampler({.addressMode = vk::SamplerAddressMode::eRepeat, .name = "sampler"});
  cubemapSampler = etna::Sampler(
    {.filter = vk::Filter::eLinear,
     .addressMode = vk::SamplerAddressMode::eRepeat,
     .name = "sampler"});


  oneShotCommands = etna::get_context().createOneShotCmdMgr();

  transferHelper =
    std::make_unique<etna::BlockingTransferHelper>(etna::BlockingTransferHelper::CreateInfo{
      .stagingSize = maxTextureResolution.x * maxTextureResolution.y * 4 * 6,
    });
}

void App::loadTextures()
{
  // Load image
  uint32_t layerCount = 1;
  char swordTextureFilename[] = LOCAL_SHADERTOY2_TEXTURES "test_tex_1.png";
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

  // Create Image for sword texture
  swordTexture = etna::get_context().createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
    .name = "sword_texture_image",
    .format = vk::Format::eR8G8B8A8Srgb,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
      vk::ImageUsageFlagBits::eTransferSrc,
    .mipLevels = mipLevels});

  localCopyBufferToImage(swordTextureBuffer, swordTexture, layerCount);

  generateMipmapsVkStyle(swordTexture, mipLevels, layerCount);

  stbi_image_free(swordTextureData);
}

void App::loadCubemap()
{
  const uint32_t layerCount = 6;
  std::string path = LOCAL_SHADERTOY2_TEXTURES "Cubemaps/Moonlight/";
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

  const vk::DeviceSize cubemapSize = width * height * 4 * layerCount; // layerCount images
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

  // Create cubemap Image
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

  generateMipmapsVkStyle(cubemapTexture, mipLevels, layerCount);

  for (int i = 0; i < 6; i++)
  {
    stbi_image_free(textures[i]);
  }
}


void App::localCopyBufferToImage(
  const etna::Buffer& buffer, const etna::Image& image, uint32_t layer_count)
{

  // Copy buffer into image
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
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void App::generateMipmapsVkStyle(
  const etna::Image& image, uint32_t mip_levels, uint32_t layer_count)
{
  auto extent = image.getExtent();

  auto commandBuffer = oneShotCommands->start();

  auto vkImage = image.get();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    int32_t mipWidth = extent.width;
    int32_t mipHeight = extent.height;

    vk::ImageMemoryBarrier barrier{
      .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
      .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
      .image = vkImage,
      .subresourceRange = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = layer_count,
      }};

    for (uint32_t i = 1; i < mip_levels; i++)
    {
      barrier.subresourceRange.baseMipLevel = i - 1;
      barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
      barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
      barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

      commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eTransfer,
        vk::DependencyFlagBits::eByRegion,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);

      std::array srcOffset = {vk::Offset3D{0, 0, 0}, vk::Offset3D{mipWidth, mipHeight, 1}};

      auto srcImageSubrecourceLayers = vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = i - 1,
        .baseArrayLayer = 0,
        .layerCount = layer_count};

      std::array dstOffset = {
        vk::Offset3D{0, 0, 0},
        vk::Offset3D{mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1}};

      auto dstImageSubrecourceLayers = vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = i,
        .baseArrayLayer = 0,
        .layerCount = layer_count};

      auto imageBlit = vk::ImageBlit{
        .srcSubresource = srcImageSubrecourceLayers,
        .srcOffsets = srcOffset,
        .dstSubresource = dstImageSubrecourceLayers,
        .dstOffsets = dstOffset};

      commandBuffer.blitImage(
        vkImage,
        vk::ImageLayout::eTransferSrcOptimal,
        vkImage,
        vk::ImageLayout::eTransferDstOptimal,
        1,
        &imageBlit,
        vk::Filter::eLinear);

      barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
      barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
      barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
      barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

      commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eTransfer,
        vk::DependencyFlagBits::eByRegion,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);

      if (mipWidth > 1)
      {
        mipWidth /= 2;
      }
      if (mipHeight > 1)
      {
        mipHeight /= 2;
      }
    }
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}
