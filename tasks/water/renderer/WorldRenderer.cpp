#include "WorldRenderer.hpp"

#include <imgui.h>
#include <tracy/Tracy.hpp>
#include <stb_image.h>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>

#include "render_utils/Utilities.hpp"


WorldRenderer::WorldRenderer()
  : lightModule()
  , staticMeshesRenderModule()
  , terrainGeneratorModule()
  , terrainRenderModule({.amplifier = 200.0f, .offset = 0.6f})
  , tonemappingModule()
  , renderTargetFormat(vk::Format::eB10G11R11UfloatPack32)
  , wireframeEnabled(false)
  , tonemappingEnabled(false)
{
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

  renderTarget = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "render_target",
    .format = renderTargetFormat,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
  });

  gBuffer.emplace(resolution, renderTargetFormat);

  constantsBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(UniformParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = fmt::format("constants{}", i)});
  });

  oneShotCommands = ctx.createOneShotCmdMgr();

  transferHelper = std::make_unique<etna::BlockingTransferHelper>(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 6});

  lightModule.allocateResources();
  staticMeshesRenderModule.allocateResources();
  terrainGeneratorModule.allocateResources();
  terrainRenderModule.allocateResources();
  tonemappingModule.allocateResources();
}

// call only after loadShaders(...)
void WorldRenderer::loadScene(std::filesystem::path path)
{
  staticMeshesRenderModule.loadScene(path);

  terrainGeneratorModule.execute({8, 8});

  lightModule.displaceLights(
    terrainRenderModule.getHeightParamsBuffer(),
    terrainGeneratorModule.getMap(),
    terrainGeneratorModule.getNormalMap(),
    terrainGeneratorModule.getSampler());
}

void WorldRenderer::loadShaders()
{
  lightModule.loadShaders();
  staticMeshesRenderModule.loadShaders();
  terrainGeneratorModule.loadShaders();
  terrainRenderModule.loadShaders();
  tonemappingModule.loadShaders();


  etna::create_program(
    "deferred_shading",
    {PROJECT_RENDERER_SHADERS_ROOT "decoy.vert.spv",
     PROJECT_RENDERER_SHADERS_ROOT "shading.frag.spv"});
}

void WorldRenderer::setupRenderPipelines()
{
  lightModule.setupPipelines();
  staticMeshesRenderModule.setupPipelines(wireframeEnabled, renderTargetFormat);
  terrainGeneratorModule.setupPipelines();
  terrainRenderModule.setupPipelines(wireframeEnabled, renderTargetFormat);
  tonemappingModule.setupPipelines();

  auto& pipelineManager = etna::get_context().getPipelineManager();

  deferredShadingPipeline = pipelineManager.createGraphicsPipeline(
    "deferred_shading",
    etna::GraphicsPipeline::CreateInfo{
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = vk::PolygonMode::eFill,
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {renderTargetFormat},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });
}

void WorldRenderer::rebuildRenderPipelines()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getQueue().waitIdle());

  setupRenderPipelines();
}

void WorldRenderer::loadCubemap()
{
  const uint32_t layerCount = 6;
  std::string path = GRAPHICS_COURSE_RESOURCES_ROOT "/textures/Cubemaps/Glacier/";
  std::vector<std::string> filenames = {
    path + "glacier_front.bmp",
    path + "glacier_back.bmp",
    path + "glacier_up.bmp",
    path + "glacier_down.bmp",
    path + "glacier_left.bmp",
    path + "glacier_right.bmp",
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

  RenderUtility::localCopyBufferToImage(
    *oneShotCommands, cubemapBuffer, cubemapTexture, layerCount);

  RenderUtility::generateMipmapsVkStyle(*oneShotCommands, cubemapTexture, mipLevels, layerCount);

  for (int i = 0; i < 6; i++)
  {
    stbi_image_free(textures[i]);
  }
}


void WorldRenderer::debugInput(const Keyboard& keyboard)
{
  if (keyboard[KeyboardKey::kF3] == ButtonState::Falling)
  {
    wireframeEnabled = !wireframeEnabled;

    rebuildRenderPipelines();
  }
}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    params.view = packet.mainCam.viewTm();
    params.invView = glm::inverse(params.view);
    params.proj = packet.mainCam.projTm(aspect);
    params.invProj = glm::inverse(params.proj);
    params.projView = params.proj * params.view;
    params.invProjView = glm::inverse(params.projView);
    params.invProjViewMat3 = glm::mat4x4(glm::inverse(glm::mat3x3(params.projView)));
    params.cameraWorldPosition = packet.mainCam.position;
    // spdlog::info("camera position - {}, {}, {}", params.cameraWorldPosition.x,
    // params.cameraWorldPosition.y, params.cameraWorldPosition.z);
    renderPacket = {.projView = params.projView, .cameraWorldPosition = params.cameraWorldPosition};
  }
}

void WorldRenderer::drawGui()
{
  ImGui::Begin("Render Settings");

  ImGui::Text(
    "Application average %.3f ms/frame (%.1f FPS)",
    1000.0f / ImGui::GetIO().Framerate,
    ImGui::GetIO().Framerate);

  ImGui::Text(
    "Camera World Position - x:%f ,y:%f ,z:%f",
    params.cameraWorldPosition.x,
    params.cameraWorldPosition.y,
    params.cameraWorldPosition.z);

  ImGui::SeparatorText("Settings");

  lightModule.drawGui(
    terrainRenderModule.getHeightParamsBuffer(),
    terrainGeneratorModule.getMap(),
    terrainGeneratorModule.getNormalMap(),
    terrainGeneratorModule.getSampler());
  staticMeshesRenderModule.drawGui();
  terrainGeneratorModule.drawGui();
  terrainRenderModule.drawGui();

  if (ImGui::CollapsingHeader("World Render Settings"))
  {
    if (ImGui::Checkbox("Enable Wireframe Mode", &wireframeEnabled))
    {
      rebuildRenderPipelines();
    }
    ImGui::Checkbox("Enable Tonemapping", &tonemappingEnabled);
  }

  ImGui::End();
}

void WorldRenderer::deferredShading(
  vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("deferred_shading");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, constants.genBinding()},
     gBuffer->genAlbedoBinding(1),
     gBuffer->genNormalBinding(2),
     gBuffer->genMaterialBinding(3),
     gBuffer->genDepthBinding(4),
     etna::Binding{5, lightModule.getPointLightsBuffer().genBinding()},
     etna::Binding{6, lightModule.getDirectionalLightsBuffer().genBinding()},
     etna::Binding{7, lightModule.getLightParamsBuffer().genBinding()},
     etna::Binding{
       8,
       cubemapTexture.genBinding(
         staticMeshesRenderModule.getStaticMeshSampler().get(),
         vk::ImageLayout::eShaderReadOnlyOptimal,
         {.type = vk::ImageViewType::eCube})}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants(
    pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(glm::uvec2), &resolution);

  cmd_buf.draw(3, 1, 0, 0);
}

void WorldRenderer::renderWorld(vk::CommandBuffer cmd_buf, vk::Image target_image)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderDeferred);

    auto& currentConstants = constantsBuffer->get();
    currentConstants.map();
    std::memcpy(currentConstants.data(), &params, sizeof(UniformParams));
    currentConstants.unmap();

    etna::set_state(
      cmd_buf,
      terrainGeneratorModule.getMap().get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    gBuffer->prepareForRender(cmd_buf);

    etna::flush_barriers(cmd_buf);

    terrainRenderModule.execute(
      cmd_buf,
      renderPacket,
      resolution,
      gBuffer->genColorAttachmentParams(),
      gBuffer->genDepthAttachmentParams(),
      terrainGeneratorModule.getMap(),
      terrainGeneratorModule.getNormalMap(),
      terrainGeneratorModule.getSampler());

    staticMeshesRenderModule.execute(
      cmd_buf,
      renderPacket,
      resolution,
      gBuffer->genColorAttachmentParams(vk::AttachmentLoadOp::eLoad),
      gBuffer->genDepthAttachmentParams(vk::AttachmentLoadOp::eLoad));

    gBuffer->prepareForRead(cmd_buf);

    etna::set_state(
      cmd_buf,
      renderTarget.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      terrainGeneratorModule.getMap().get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmd_buf);

    {
      ETNA_PROFILE_GPU(cmd_buf, deferredShading);
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = renderTarget.get(), .view = renderTarget.getView({})}},
        {});

      cmd_buf.bindPipeline(
        vk::PipelineBindPoint::eGraphics, deferredShadingPipeline.getVkPipeline());
      deferredShading(cmd_buf, currentConstants, deferredShadingPipeline.getVkPipelineLayout());
    }

    if (tonemappingEnabled)
    {
      tonemappingModule.execute(cmd_buf, renderTarget, resolution);
    }

    etna::set_state(
      cmd_buf,
      renderTarget.get(),
      vk::PipelineStageFlagBits2::eTransfer,
      vk::AccessFlagBits2::eTransferRead,
      vk::ImageLayout::eTransferSrcOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      target_image,
      vk::PipelineStageFlagBits2::eTransfer,
      vk::AccessFlagBits2::eTransferWrite,
      vk::ImageLayout::eTransferDstOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmd_buf);

    {
      std::array srcOffset = {
        vk::Offset3D{},
        vk::Offset3D{static_cast<int32_t>(resolution.x), static_cast<int32_t>(resolution.y), 1}};
      auto srdImageSubrecourceLayers = vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1};

      std::array dstOffset = {
        vk::Offset3D{},
        vk::Offset3D{static_cast<int32_t>(resolution.x), static_cast<int32_t>(resolution.y), 1}};
      auto dstImageSubrecourceLayers = vk::ImageSubresourceLayers{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1};

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
        .srcImage = renderTarget.get(),
        .srcImageLayout = vk::ImageLayout::eTransferSrcOptimal,
        .dstImage = target_image,
        .dstImageLayout = vk::ImageLayout::eTransferDstOptimal,
        .regionCount = 1,
        .pRegions = &imageBlit,
        .filter = vk::Filter::eLinear};

      cmd_buf.blitImage2(&blitInfo);
    }
  }
}
