#include "WorldRenderer.hpp"

#include <imgui.h>
#include <tracy/Tracy.hpp>
#include <stb_image.h>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Assert.hpp>

#include "render_utils/Utilities.hpp"


WorldRenderer::WorldRenderer(const InitInfo& info)
  : lightModule()
  , staticMeshesRenderModule()
  , terrainGeneratorModule()
  , terrainRenderModule()
  , tonemappingModule()
  , waterGeneratorModule()
  , waterRenderModule()
  , renderTargetFormat(info.renderTargetFormat)
  , wireframeEnabled(info.wireframeEnabled)
  , tonemappingEnabled(info.tonemappingEnabled)
  , timeStopped(info.timeStopped)
  , shadowCascadesAmount(info.shadowCascadesAmount)
{
  ETNA_VERIFYF(shadowCascadesAmount > 0, "Shadow cascades amount should be greater than 0");
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;
  params = {};
  params.colorShadows = 0;
  params.usePCF = 1;
  params.pcfRange = 1;

  auto& ctx = etna::get_context();

  renderTarget = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "render_target",
      .format = renderTargetFormat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
    });

  gBuffer.emplace(
    GBuffer::CreateInfo{
      .resolution = swapchain_resolution,
      .shadowMapsResolution = glm::uvec2(2048, 2048),
      .renderTargetFormat = renderTargetFormat,
      .normalsFormat = vk::Format::eR16G16B16A16Snorm,
      .shadowsFormat = vk::Format::eD16Unorm,
      .shadowCascadesAmount = shadowCascadesAmount});

  constantsBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(
      etna::Buffer::CreateInfo{
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
  waterGeneratorModule.allocateResources();
  waterRenderModule.allocateResources();
}

// call only after loadShaders(...)
void WorldRenderer::loadScene(std::filesystem::path path, float near_plane, float far_plane)
{
  staticMeshesRenderModule.loadScene(path);

  getPlanesForShadowCascades(near_plane, far_plane, 0.9f);

  for (uint32_t i = 0; i < shadowCascadesAmount + 1; i++)
  {
    spdlog::info("plane {} - {}", i, planes[i]);
  }

  loadInfo();
}

void WorldRenderer::loadInfo()
{
  terrainGeneratorModule.execute();

  staticMeshesRenderModule.loadSet();

  lightModule.loadMaps(terrainGeneratorModule.getBindings(vk::ImageLayout::eGeneral));

  lightModule.loadLights(
    // {Light{.pos = {0, 1, 0}, .radius = 0,  .color = {1, 1, 1}, .intensity = 15},
    //  Light{.pos = {0, 0, 5}, .radius = 0,  .color = {1, 0, 1}, .intensity = 15},
    //  Light{.pos = {5, 0, 25}, .radius = 0, .color = {1, 1, 1}, .intensity = 15},
    //  Light{.pos = {3, 2, 50}, .radius = 0, .color = {0.5, 1, 0.5}, .intensity = 15},
    //  Light{.pos = {75, 2, 75}, .radius = 0, .color = {1, 0.5, 1}, .intensity = 15},
    //  Light{.pos = {50, 2, 20}, .radius = 0, .color = {0, 1, 1}, .intensity = 15},
    //  Light{.pos = {25, 2, 50}, .radius = 0, .color = {1, 1, 0}, .intensity = 15},
    //  Light{.pos = {50, 2, 50}, .radius = 0, .color = {0.3, 1, 0}, .intensity = 15},
    //  Light{.pos = {25, 2, 10}, .radius = 0, .color = {1, 1, 0}, .intensity = 15},
    //  Light{
    //    .pos = {100, 2, 100}, .radius = 0, .color = {1, 0.5, 0.5}, .intensity = 15},
    //  Light{.pos = {150, 2, 150}, .radius = 0, .color = {1, 1, 1}, .intensity = 100},
    //  Light{.pos = {25, 2, 10}, .radius = 0, .color = {1, 1, 0}, .intensity = 15},
    //  Light{.pos = {10, 2, 25}, .radius = 0, .color = {1, 0, 1}, .intensity = 15}},
    // {DirectionalLight{
    //   .direction = glm::vec3{1, -0.35, -3}, .intensity = 1.0f, .color = glm::vec3{1, 0.694,
    //   0.32}}},
    {},
    {},
    ShadowCastingDirectionalLight::CreateInfo{
      .light =
        DirectionalLight{
          .direction = glm::normalize(glm::vec3{1, -0.6, -3}),
          .intensity = 1.0f,
          .color = glm::vec3{1, 0.694, 0.32}},
      .planes = planes,
      .shadowMapSize = static_cast<float>(gBuffer->getShadowTextureExtent().width)});

  lightModule.displaceLights();

  terrainRenderModule.loadMaps(
    terrainGeneratorModule.getBindings(vk::ImageLayout::eShaderReadOnlyOptimal));

  // waterGeneratorModule.executeStart();
}

void WorldRenderer::loadShaders()
{
  lightModule.loadShaders();
  staticMeshesRenderModule.loadShaders();
  terrainGeneratorModule.loadShaders();
  terrainRenderModule.loadShaders();
  tonemappingModule.loadShaders();
  waterGeneratorModule.loadShaders();
  waterRenderModule.loadShaders();

  etna::create_program(
    "deferred_shading",
    {PROJECT_RENDERER_SHADERS_ROOT "decoy.vert.spv",
     PROJECT_RENDERER_SHADERS_ROOT "shading.frag.spv"});
}

void WorldRenderer::setupRenderPipelines()
{
  lightModule.setupPipelines();
  staticMeshesRenderModule.setupPipelines(
    wireframeEnabled, renderTargetFormat, gBuffer->getShadowTextureFormat());
  terrainGeneratorModule.setupPipelines();
  terrainRenderModule.setupPipelines(
    wireframeEnabled, renderTargetFormat, gBuffer->getShadowTextureFormat());
  tonemappingModule.setupPipelines();
  waterGeneratorModule.setupPipelines();
  waterRenderModule.setupPipelines(wireframeEnabled, renderTargetFormat);

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
  std::string path = GRAPHICS_COURSE_RESOURCES_ROOT "/textures/Cubemaps/Sea/";
  std::vector<std::string> filenames = {
    path + "nz.png",
    path + "pz.png",
    path + "py.png",
    path + "ny.png",
    path + "px.png",
    path + "nx.png",
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

  etna::Buffer cubemapBuffer = etna::get_context().createBuffer(
    etna::Buffer::CreateInfo{
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

  cubemapTexture = etna::get_context().createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1},
      .name = "cubemap_image",
      .format = vk::Format::eR8G8B8A8Srgb,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
        vk::ImageUsageFlagBits::eTransferSrc,
      .layers = layerCount,
      .mipLevels = mipLevels,
      .flags = vk::ImageCreateFlagBits::eCubeCompatible});

  render_utility::local_copy_buffer_to_image(
    *oneShotCommands, cubemapBuffer, cubemapTexture, layerCount);

  render_utility::generate_mipmaps_vk_style(
    *oneShotCommands, cubemapTexture, mipLevels, layerCount);

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
    renderPacket = {
      .projView = params.projView,
      .cameraWorldPosition = params.cameraWorldPosition,
      .time = packet.currentTime,
      .resolution = resolution};

    if (!timeStopped)
    {
      lightModule.update(packet.mainCam, aspect);
    }
  }
}

void WorldRenderer::drawGui()
{
  static bool colorShadow = false;
  static bool usePCF = true;

  ImGui::Begin("Application Settings");

  ImGui::Text(
    "Application average %.3f ms/frame (%.1f FPS)",
    1000.0f / ImGui::GetIO().Framerate,
    ImGui::GetIO().Framerate);

  ImGui::Text(
    "Camera World Position - x:%f ,y:%f ,z:%f",
    params.cameraWorldPosition.x,
    params.cameraWorldPosition.y,
    params.cameraWorldPosition.z);

  ImGui::SeparatorText("Specific Settings");

  lightModule.drawGui();
  staticMeshesRenderModule.drawGui();
  terrainGeneratorModule.drawGui();
  terrainRenderModule.drawGui();
  waterGeneratorModule.drawGui();
  waterRenderModule.drawGui();

  ImGui::SeparatorText("Shadow Settings");

  if (ImGui::Checkbox("Enable colored shadows", &colorShadow))
  {
    params.colorShadows = static_cast<shader_bool>(colorShadow);
  }
  if (ImGui::Checkbox("Use PCF for shadows", &usePCF))
  {
    params.usePCF = static_cast<shader_bool>(usePCF);
  }
  int pcfRange = params.pcfRange;
  ImGui::SliderInt("PCF Radius", &pcfRange, 0, 4);
  params.pcfRange = pcfRange;

  ImGui::SeparatorText("General Settings");


  if (ImGui::Checkbox("Enable Wireframe Mode", &wireframeEnabled))
  {
    rebuildRenderPipelines();
  }
  ImGui::Checkbox("Enable Tonemapping", &tonemappingEnabled);
  ImGui::Checkbox("Stop Time", &timeStopped);


  ImGui::End();
}

void WorldRenderer::deferredShading(
  vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("deferred_shading");

  std::vector<etna::Binding> bindings;
  bindings.reserve(4 + shadowCascadesAmount);

  bindings.emplace_back(gBuffer->genAlbedoBinding(0));
  bindings.emplace_back(gBuffer->genNormalBinding(1));
  bindings.emplace_back(gBuffer->genMaterialBinding(2));
  bindings.emplace_back(gBuffer->genDepthBinding(3));

  std::vector<etna::Binding> shadowBindings = gBuffer->genShadowBindings(4);

  for (std::size_t i = 0; i < shadowBindings.size(); i++)
  {
    bindings.emplace_back(std::move(shadowBindings[i]));
  }

  auto gSet = etna::create_descriptor_set(shaderInfo.getDescriptorLayoutId(0), cmd_buf, bindings);

  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(1),
    cmd_buf,
    {etna::Binding{0, constants.genBinding()},
     etna::Binding{1, lightModule.getPointLightsBuffer().genBinding()},
     etna::Binding{2, lightModule.getDirectionalLightsBuffer().genBinding()},
     etna::Binding{3, lightModule.getShadowCastingDirLightInfoBuffer().genBinding()},
     etna::Binding{4, lightModule.getLightParamsBuffer().genBinding()},
     etna::Binding{
       5,
       cubemapTexture.genBinding(
         staticMeshesRenderModule.getStaticMeshSampler().get(),
         vk::ImageLayout::eShaderReadOnlyOptimal,
         {.type = vk::ImageViewType::eCube})}});

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, {gSet.getVkSet(), set.getVkSet()}, {});

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

    lightModule.prepareForDraw();

    // if (!timeStopped)
    // {
    //   waterGeneratorModule.executeProgress(cmd_buf, renderPacket.time);
    // }

    etna::set_state(
      cmd_buf,
      waterGeneratorModule.getHeightMap().get(),
      vk::PipelineStageFlagBits2::eTessellationControlShader |
        vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
        vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      cmd_buf,
      waterGeneratorModule.getNormalMap().get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    gBuffer->prepareForRender(cmd_buf);

    etna::flush_barriers(cmd_buf);

    if (!timeStopped)
    {
      for (uint32_t i = 0; i < shadowCascadesAmount; i++)
      {
        staticMeshesRenderModule.executeShadowMapping(
          cmd_buf,
          gBuffer->getShadowTextureExtent(),
          lightModule.getShadowCastingDirLightMatrixBinding(9, i),
          gBuffer->genShadowMappingAttachmentParams(i));

        terrainRenderModule.executeShadowMapping(
          cmd_buf,
          renderPacket,
          gBuffer->getShadowTextureExtent(),
          lightModule.getShadowCastingDirLightMatrixBinding(1, i),
          gBuffer->genShadowMappingAttachmentParams(i, vk::AttachmentLoadOp::eLoad));
      }
    }

    terrainRenderModule.executeRender(
      cmd_buf,
      renderPacket,
      gBuffer->genColorAttachmentParams(),
      gBuffer->genDepthAttachmentParams());

    staticMeshesRenderModule.executeRender(
      cmd_buf,
      renderPacket,
      gBuffer->genColorAttachmentParams(vk::AttachmentLoadOp::eLoad),
      gBuffer->genDepthAttachmentParams(vk::AttachmentLoadOp::eLoad));

    etna::set_state(
      cmd_buf,
      renderTarget.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    gBuffer->prepareForRead(cmd_buf);

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

    gBuffer->continueDepthWrite(cmd_buf);

    etna::flush_barriers(cmd_buf);

    // waterRenderModule.executeRender(
    //   cmd_buf,
    //   renderPacket,
    //   {{.image = renderTarget.get(),
    //     .view = renderTarget.getView({}),
    //     .loadOp = vk::AttachmentLoadOp::eLoad}},
    //   gBuffer->genDepthAttachmentParams(vk::AttachmentLoadOp::eLoad),
    //   waterGeneratorModule.getHeightMap(),
    //   waterGeneratorModule.getNormalMap(),
    //   gBuffer->genShadowBindings(4), // change later
    //   waterGeneratorModule.getSampler(),
    //   lightModule.getShadowCastingDirLightInfoBuffer(),
    //   cubemapTexture);

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

    render_utility::blit_image(
      cmd_buf,
      renderTarget.get(),
      target_image,
      vk::Offset3D{static_cast<int32_t>(resolution.x), static_cast<int32_t>(resolution.y), 1});
  }
}

void WorldRenderer::getPlanesForShadowCascades(float near_plane, float far_plane, float weight)
{
  planes.reserve(shadowCascadesAmount + 1);

  planes.emplace_back(near_plane);
  for (uint32_t i = 1; i < shadowCascadesAmount; i++)
  {
    float interpolation = static_cast<float>(i) / static_cast<float>(shadowCascadesAmount);

    float logPart = near_plane * glm::pow(far_plane / near_plane, interpolation);
    float uniformPart = (far_plane - near_plane) * interpolation;

    float plane = logPart * weight + (1.0f - weight) * uniformPart;

    planes.emplace_back(plane);
  }
  planes.emplace_back(far_plane);
}
