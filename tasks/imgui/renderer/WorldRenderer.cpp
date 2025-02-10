#include "WorldRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>

#include "imgui.h"
#include "shaders/postprocessing/UniformHistogramInfo.h"

WorldRenderer::WorldRenderer()
  : sceneMgr{std::make_unique<SceneManager>()}
  , renderTargetFormat(vk::Format::eB10G11R11UfloatPack32)
  , maxInstancesInScene{4096}
  , binsAmount(128)
  , wireframeEnabled(false)
{
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

  mainViewDepth = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "main_view_depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
  });

  renderTarget = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "render_target",
    .format = renderTargetFormat,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled |
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc,
  });

  params.terrainInChunks = shader_uvec2(64, 64);
  params.chunk = shader_uvec2(16, 16);

  instanceMatricesBuffer.emplace(
    ctx.getMainWorkCount(), [&ctx, maxInstancesInScene = this->maxInstancesInScene](std::size_t i) {
      return ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = sizeof(glm::mat4x4) * maxInstancesInScene,
        .bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer |
          vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU,
        .name = fmt::format("sameInstanceMatrices{}", i)});
    });

  constantsBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(UniformParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = fmt::format("constants{}", i)});
  });

  histogramBuffer.emplace(
    ctx.getMainWorkCount(), [&ctx, binsAmount = this->binsAmount](std::size_t i) {
      return ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = binsAmount * sizeof(int32_t),
        .bufferUsage =
          vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        .name = fmt::format("histogram{}", i)});
    });

  histogramInfoBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(UniformHistogramInfo),
      .bufferUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = fmt::format("histogram_info{}", i)});
  });

  distributionBuffer.emplace(
    ctx.getMainWorkCount(), [&ctx, binsAmount = this->binsAmount](std::size_t i) {
      return ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = binsAmount * sizeof(float),
        .bufferUsage =
          vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        .name = fmt::format("distribution{}", i)});
    });

  terrainSampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "terrain_sampler"});

  oneShotCommands = ctx.createOneShotCmdMgr();

  instancesAmount.resize(maxInstancesInScene, 0);
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  sceneMgr->selectBakedScene(path);
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "static_mesh_material",
    {IMGUI_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     IMGUI_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh", {IMGUI_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program(
    "terrain_generator",
    {IMGUI_RENDERER_SHADERS_ROOT "decoy.vert.spv",
     IMGUI_RENDERER_SHADERS_ROOT "generator.frag.spv"});

  etna::create_program(
    "terrain_render",
    {IMGUI_RENDERER_SHADERS_ROOT "chunk.vert.spv",
     IMGUI_RENDERER_SHADERS_ROOT "subdivide_chunk.tesc.spv",
     IMGUI_RENDERER_SHADERS_ROOT "process_chunk.tese.spv",
     IMGUI_RENDERER_SHADERS_ROOT "terrain.frag.spv"});

  etna::create_program(
    "min_max_calculation", {IMGUI_RENDERER_SHADERS_ROOT "calculate_min_max.comp.spv"});
  etna::create_program("histogram_calculation", {IMGUI_RENDERER_SHADERS_ROOT "histogram.comp.spv"});
  etna::create_program(
    "histogram_processing", {IMGUI_RENDERER_SHADERS_ROOT "process_histogram.comp.spv"});
  etna::create_program("postprocess_compute", {IMGUI_RENDERER_SHADERS_ROOT "postprocess.comp.spv"});
}

void WorldRenderer::setupRenderPipelines()
{
  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = sceneMgr->getVertexFormatDescription(),
    }},
  };

  auto& pipelineManager = etna::get_context().getPipelineManager();

  staticMeshPipeline = {};
  staticMeshPipeline = pipelineManager.createGraphicsPipeline(
    "static_mesh_material",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput = sceneVertexInputDesc,
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = (wireframeEnabled ? vk::PolygonMode::eLine : vk::PolygonMode::eFill),
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

  terrainRenderPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_render",
    etna::GraphicsPipeline::CreateInfo{
      .inputAssemblyConfig = {.topology = vk::PrimitiveTopology::ePatchList},
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = (wireframeEnabled ? vk::PolygonMode::eLine : vk::PolygonMode::eFill),
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

  calculateMinMaxPipeline = pipelineManager.createComputePipeline("min_max_calculation", {});
  histogramPipeline = pipelineManager.createComputePipeline("histogram_calculation", {});
  processHistogramPipeline = pipelineManager.createComputePipeline("histogram_processing", {});
  postprocessComputePipeline = pipelineManager.createComputePipeline("postprocess_compute", {});
}

void WorldRenderer::setupTerrainGeneration(vk::Format texture_format, vk::Extent3D extent)
{
  auto& ctx = etna::get_context();
  auto& pipelineManager = ctx.getPipelineManager();

  terrainMap = ctx.createImage(etna::Image::CreateInfo{
    .extent = extent,
    .name = "terrain_map",
    .format = texture_format,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment});

  terrainGenerationPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_generator",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {texture_format},
      }});

  params.extent = shader_uvec2(extent.width, extent.height);
}

void WorldRenderer::generateTerrain()
{
  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    {
      auto extent = terrainMap.getExtent();
      glm::uvec2 glmExtent = {extent.width, extent.height};
      etna::RenderTargetState state(
        commandBuffer,
        {{}, {glmExtent.x, glmExtent.y}},
        {{terrainMap.get(), terrainMap.getView({})}},
        {});

      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics, terrainGenerationPipeline.getVkPipeline());

      commandBuffer.pushConstants<glm::uvec2>(
        terrainGenerationPipeline.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eFragment,
        0,
        {glmExtent});

      commandBuffer.draw(3, 1, 0, 0);
    }

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void WorldRenderer::debugInput(const Keyboard& keyboard)
{
  if (keyboard[KeyboardKey::kF3] == ButtonState::Falling)
  {
    ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());

    wireframeEnabled = !wireframeEnabled;

    setupRenderPipelines();

    ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
  }
}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;
  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
    params.projView = worldViewProj;
    params.cameraWorldPosition = packet.mainCam.position;
  }
}

void WorldRenderer::drawGui()
{
  ImGui::Begin("Render Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

  ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Press F3 to enable wireframe mode");
  ImGui::NewLine();
  ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Press B to recompile shaders");
  
  ImGui::End();
}

void WorldRenderer::renderScene(
  vk::CommandBuffer cmd_buf,
  const glm::mat4x4& glob_tm,
  vk::PipelineLayout pipeline_layout,
  etna::Buffer& instance_buffer)
{
  ZoneScoped;
  if (!sceneMgr->getVertexBuffer())
    return;

  cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  cmd_buf.pushConstants<glm::mat4x4>(
    pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, {glob_tm});

  auto shaderInfo = etna::get_shader_program("static_mesh_material");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0), cmd_buf, {etna::Binding{0, instance_buffer.genBinding()}});
  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  uint32_t offset = 0;

  auto relems = sceneMgr->getRenderElements();

  for (uint32_t i = 0; i < relems.size(); i++)
  {
    if (instancesAmount[i] > 0)
    {
      cmd_buf.drawIndexed(
        relems[i].indexCount,
        instancesAmount[i],
        relems[i].indexOffset,
        relems[i].vertexOffset,
        offset);
      offset += instancesAmount[i];
    }
  }

  // instancesAmount.clear();
  std::memset(instancesAmount.data(), 0, relems.size() * sizeof(uint32_t));
}

void WorldRenderer::renderTerrain(
  vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("terrain_render");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,

    {etna::Binding{0, constants.genBinding()},
     etna::Binding{
       1, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.draw(4, params.terrainInChunks.x * params.terrainInChunks.y, 0, 0);
}

void WorldRenderer::renderWorld(vk::CommandBuffer cmd_buf, vk::Image target_image)
// [[maybe_unused]] vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    // auto& currentBuffer = instanceMatricesBuffer->get();
    // parseInstanceInfo(currentBuffer, worldViewProj);

    auto& currentConstants = constantsBuffer->get();
    updateConstants(currentConstants);

    etna::set_state(
      cmd_buf,
      renderTarget.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmd_buf);

    {
      ETNA_PROFILE_GPU(cmd_buf, renderTerrain);
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = renderTarget.get(), .view = renderTarget.getView({})}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainRenderPipeline.getVkPipeline());
      renderTerrain(cmd_buf, currentConstants, terrainRenderPipeline.getVkPipelineLayout());
    }

    // {
    //   ETNA_PROFILE_GPU(cmd_buf, renderMeshes);
    //   etna::RenderTargetState renderTargets(
    //     cmd_buf,
    //     {{0, 0}, {resolution.x, resolution.y}},
    //     {{.image = target_image, .view = target_image_view, .loadOp =
    //     vk::AttachmentLoadOp::eLoad}},
    //     {.image = mainViewDepth.get(), .view = mainViewDepth.getView({}), .loadOp =
    //     vk::AttachmentLoadOp::eLoad});

    //   cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());
    //   renderScene(cmd_buf, worldViewProj, staticMeshPipeline.getVkPipelineLayout(),
    //   currentBuffer);
    // }

    auto& currentHistogramBuffer = histogramBuffer->get();
    auto& currentDistributionBuffer = distributionBuffer->get();
    auto& currentHistogramInfo = histogramInfoBuffer->get();

    cmd_buf.fillBuffer(currentHistogramBuffer.get(), 0, vk::WholeSize, 0);
    cmd_buf.fillBuffer(currentDistributionBuffer.get(), 0, vk::WholeSize, 0);
    cmd_buf.fillBuffer(currentHistogramInfo.get(), 0, vk::WholeSize, 0);

    {
      std::array bufferBarriers = {
        vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
          .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
          .buffer = currentHistogramBuffer.get(),
          .size = vk::WholeSize},
        vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
          .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
          .buffer = currentDistributionBuffer.get(),
          .size = vk::WholeSize},
        vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
          .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
          .buffer = currentHistogramInfo.get(),
          .size = vk::WholeSize}};

      vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
        .pBufferMemoryBarriers = bufferBarriers.data()};

      cmd_buf.pipelineBarrier2(dependencyInfo);
    }

    {
      ETNA_PROFILE_GPU(cmd_buf, tonemapping);
      tonemappingShaderStart(
        cmd_buf,
        calculateMinMaxPipeline,
        "min_max_calculation",
        {etna::Binding{0, renderTarget.genBinding({}, vk::ImageLayout::eGeneral)},
         etna::Binding{1, currentHistogramInfo.genBinding()}},
        binsAmount,
        {(resolution.x + 31) / 32, (resolution.y + 31) / 32});

      {
        std::array bufferBarriers = {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .buffer = currentHistogramInfo.get(),
          .size = vk::WholeSize}};

        vk::DependencyInfo dependencyInfo = {
          .dependencyFlags = vk::DependencyFlagBits::eByRegion,
          .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
          .pBufferMemoryBarriers = bufferBarriers.data()};

        cmd_buf.pipelineBarrier2(dependencyInfo);
      }

      tonemappingShaderStart(
        cmd_buf,
        histogramPipeline,
        "histogram_calculation",
        {etna::Binding{0, renderTarget.genBinding({}, vk::ImageLayout::eGeneral)},
         etna::Binding{1, currentHistogramBuffer.genBinding()},
         etna::Binding{2, currentHistogramInfo.genBinding()}},
        binsAmount,
        {(resolution.x + 31) / 32, (resolution.y + 31) / 32});

      {
        std::array bufferBarriers = {
          vk::BufferMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .buffer = currentHistogramBuffer.get(),
            .size = vk::WholeSize},
          vk::BufferMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
            .buffer = currentHistogramInfo.get(),
            .size = vk::WholeSize}};

        vk::DependencyInfo dependencyInfo = {
          .dependencyFlags = vk::DependencyFlagBits::eByRegion,
          .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
          .pBufferMemoryBarriers = bufferBarriers.data()};

        cmd_buf.pipelineBarrier2(dependencyInfo);
      }

      tonemappingShaderStart(
        cmd_buf,
        processHistogramPipeline,
        "histogram_processing",
        {etna::Binding{0, currentHistogramBuffer.genBinding()},
         etna::Binding{1, currentDistributionBuffer.genBinding()},
         etna::Binding{2, currentHistogramInfo.genBinding()}},
        binsAmount,
        {1, 1});

      {
        std::array bufferBarriers = {
          vk::BufferMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .buffer = currentDistributionBuffer.get(),
            .size = vk::WholeSize},
          vk::BufferMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .buffer = currentHistogramInfo.get(),
            .size = vk::WholeSize}};

        vk::DependencyInfo dependencyInfo = {
          .dependencyFlags = vk::DependencyFlagBits::eByRegion,
          .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
          .pBufferMemoryBarriers = bufferBarriers.data()};

        cmd_buf.pipelineBarrier2(dependencyInfo);
      }

      etna::set_state(
        cmd_buf,
        renderTarget.get(),
        vk::PipelineStageFlagBits2::eComputeShader,
        vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite,
        vk::ImageLayout::eGeneral,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(cmd_buf);

      tonemappingShaderStart(
        cmd_buf,
        postprocessComputePipeline,
        "postprocess_compute",
        {etna::Binding{0, renderTarget.genBinding({}, vk::ImageLayout::eGeneral)},
         etna::Binding{1, currentDistributionBuffer.genBinding()},
         etna::Binding{2, currentHistogramInfo.genBinding()}},
        binsAmount,
        {(resolution.x + 31) / 32, (resolution.y + 31) / 32});
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

void WorldRenderer::parseInstanceInfo(etna::Buffer& buffer, const glm::mat4x4& glob_tm)
{
  ZoneScoped;

  auto instanceMeshes = sceneMgr->getInstanceMeshes();
  auto instanceMatrices = sceneMgr->getInstanceMatrices();
  auto meshes = sceneMgr->getMeshes();
  auto bounds = sceneMgr->getRenderElementsBounds();

  buffer.map();

  glm::mat4x4* instanceData = std::bit_cast<glm::mat4x4*>(buffer.data());

  std::size_t index = 0;
  for (std::size_t i = 0; i < instanceMatrices.size(); i++)
  {
    const auto meshIdx = instanceMeshes[i];
    const auto& currentMatrix = instanceMatrices[i];

    for (std::size_t j = 0; j < meshes[meshIdx].relemCount; j++)
    {
      const auto relemIdx = meshes[meshIdx].firstRelem + j;
      if (!isVisible(bounds[relemIdx], glob_tm, currentMatrix))
      {
        continue;
      }
      instancesAmount[relemIdx]++;

      instanceData[index] = currentMatrix;
      index++;
    }
  }

  buffer.unmap();
}

void WorldRenderer::updateConstants(etna::Buffer& constants)
{
  ZoneScoped;

  constants.map();

  std::memcpy(constants.data(), &params, sizeof(UniformParams));

  constants.unmap();
}


void WorldRenderer::tonemappingShaderStart(
  vk::CommandBuffer cmd_buf,
  const etna::ComputePipeline& current_pipeline,
  std::string shader_program,
  std::vector<etna::Binding> bindings,
  std::optional<uint32_t> push_constant,
  glm::uvec2 group_count)
{
  ZoneScoped;
  auto vkPipelineLayout = current_pipeline.getVkPipelineLayout();
  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, current_pipeline.getVkPipeline());

  auto shaderProgramInfo = etna::get_shader_program(shader_program.c_str());

  auto set =
    etna::create_descriptor_set(shaderProgramInfo.getDescriptorLayoutId(0), cmd_buf, bindings);

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, vkPipelineLayout, 0, 1, &vkSet, 0, nullptr);

  if (push_constant.has_value())
  {
    auto pushConst = push_constant.value();
    cmd_buf.pushConstants(
      vkPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pushConst), &pushConst);
  }

  etna::flush_barriers(cmd_buf);

  cmd_buf.dispatch(group_count.x, group_count.y, 1);
}

bool WorldRenderer::isVisible(
  const Bounds& bounds, const glm::mat4& proj_view, const glm::mat4& transform)
{
  std::array corners = {
    glm::vec3{1, 1, 1},
    glm::vec3{1, 1, -1},
    glm::vec3{1, -1, 1},
    glm::vec3{-1, 1, 1},
    glm::vec3{1, -1, -1},
    glm::vec3{-1, 1, -1},
    glm::vec3{-1, -1, 1},
    glm::vec3{-1, -1, -1},
  };

  glm::vec3 min = {2, 2, 2};    // > 1
  glm::vec3 max = {-2, -2, -2}; // < -1

  auto matrix = proj_view * transform;

  for (const auto& corner : corners)
  {
    glm::vec4 projection = matrix * glm::vec4(bounds.origin + (corner * bounds.extents), 1.0f);

    glm::vec3 current = {projection.x, projection.y, projection.z};
    current /= projection.w;

    min = glm::min(current, min);
    max = glm::max(current, max);
  }

  return min.z <= 1.0f && max.z >= -1.0f && min.x <= 1.0f && max.x >= -1.0f && min.y <= 1.0f &&
    max.y >= -1.0f;
}
