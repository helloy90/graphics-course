#include "WorldRenderer.hpp"
#include "etna/DescriptorSet.hpp"

#include <cstring>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <vulkan/vulkan.hpp>


WorldRenderer::WorldRenderer()
  : sceneMgr{std::make_unique<SceneManager>()}
  , renderTargetFormat(vk::Format::eB10G11R11UfloatPack32)
  , maxInstancesInScene{4096}
  , instanceMatricesBuffer{std::nullopt}
  , constantsBuffer{std::nullopt}
  , histogramBuffer{std::nullopt}
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
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
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
        .size = binsAmount * sizeof(int32_t), // assume int
        .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
        .name = fmt::format("histogram{}", i)});
    });

  binStepSizeBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = 5 * sizeof(float), //hardcode for now
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = fmt::format("binStepSize{}", i)});
  });

  distributionBuffer.emplace(
    ctx.getMainWorkCount(), [&ctx, binsAmount = this->binsAmount](std::size_t i) {
      return ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = binsAmount * sizeof(float),
        .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
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
    {TONEMAPPING_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh", {TONEMAPPING_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program(
    "terrain_generator",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "decoy.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "generator.frag.spv"});

  etna::create_program(
    "terrain_render",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "chunk.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "subdivide_chunk.tesc.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "process_chunk.tese.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "terrain.frag.spv"});

  etna::create_program(
    "histogram_calculation",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "/postprocessing/histogram.comp.spv"});
  etna::create_program(
    "histogram_processing",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "/postprocessing/process_histogram.comp.spv"});
  etna::create_program(
    "distribution_calculation",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "/postprocessing/distribution.comp.spv"});
  etna::create_program(
    "postprocess",
    {TONEMAPPING_RENDERER_SHADERS_ROOT "/decoy.vert.spv",
     TONEMAPPING_RENDERER_SHADERS_ROOT "/postprocessing/postprocess.frag.spv"});
}

void WorldRenderer::setupRenderPipelines(vk::Format swapchain_format)
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

  histogramPipeline = pipelineManager.createComputePipeline("histogram_calculation", {});
  processHistogramPipeline = pipelineManager.createComputePipeline("histogram_processing", {});
  distributionPipeline = pipelineManager.createComputePipeline("distribution_calculation", {});

  postprocessPipeline = pipelineManager.createGraphicsPipeline(
    "postprocess",
    {
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = (wireframeEnabled ? vk::PolygonMode::eLine : vk::PolygonMode::eFill),
          .cullMode = vk::CullModeFlagBits::eBack,
          .frontFace = vk::FrontFace::eCounterClockwise,
          .lineWidth = 1.f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {swapchain_format},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });
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

void WorldRenderer::debugInput(const Keyboard& keyboard, vk::Format swapchain_format)
{
  if (keyboard[KeyboardKey::kF3] == ButtonState::Falling)
  {
    ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());

    wireframeEnabled = !wireframeEnabled;

    setupRenderPipelines(swapchain_format);
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

void WorldRenderer::renderScene(
  vk::CommandBuffer cmd_buf,
  const glm::mat4x4& glob_tm,
  vk::PipelineLayout pipeline_layout,
  etna::Buffer& current_instance_buffer)
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
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, current_instance_buffer.genBinding()}});
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

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    // auto& currentBuffer = instanceMatricesBuffer->get();
    // parseInstanceInfo(currentBuffer, worldViewProj);

    auto& currentConstants = constantsBuffer->get();
    updateConstants(currentConstants);

    {
      ETNA_PROFILE_GPU(cmd_buf, renderTerrain);
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = target_image, .view = target_image_view}},
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
    cmd_buf.fillBuffer(currentHistogramBuffer.get(), 0, vk::WholeSize, 0);
    auto& binStepSize = binStepSizeBuffer->get();
    cmd_buf.fillBuffer(binStepSize.get(), 0, vk::WholeSize, 0);
    auto& currentDistributionBuffer = distributionBuffer->get();
    cmd_buf.fillBuffer(currentDistributionBuffer.get(), 0, vk::WholeSize, 0);

    {
      ETNA_PROFILE_GPU(cmd_buf, histograms);

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, histogramPipeline.getVkPipeline());
      generateHistogram(
        cmd_buf, currentHistogramBuffer, binStepSize, histogramPipeline.getVkPipelineLayout());
    }

    {
      etna::set_state(
        cmd_buf,
        renderTarget.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      etna::flush_barriers(cmd_buf);

      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = target_image, .view = target_image_view, .loadOp = vk::AttachmentLoadOp::eLoad}},
        {.image = mainViewDepth.get(),
         .view = mainViewDepth.getView({}),
         .loadOp = vk::AttachmentLoadOp::eLoad});
      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, postprocessPipeline.getVkPipeline());

      applyPostprocessing(
        cmd_buf, currentHistogramBuffer, postprocessPipeline.getVkPipelineLayout());
    }
  }
}

void WorldRenderer::parseInstanceInfo(etna::Buffer& current_buffer, const glm::mat4x4& glob_tm)
{
  ZoneScoped;

  auto instanceMeshes = sceneMgr->getInstanceMeshes();
  auto instanceMatrices = sceneMgr->getInstanceMatrices();
  auto meshes = sceneMgr->getMeshes();
  auto bounds = sceneMgr->getRenderElementsBounds();

  current_buffer.map();

  glm::mat4x4* instanceData = std::bit_cast<glm::mat4x4*>(current_buffer.data());

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

  current_buffer.unmap();
}

void WorldRenderer::updateConstants(etna::Buffer& constants)
{
  ZoneScoped;

  constants.map();

  std::memcpy(constants.data(), &params, sizeof(UniformParams));

  constants.unmap();
}

void WorldRenderer::generateHistogram(
  vk::CommandBuffer cmd_buf,
  etna::Buffer& current_histogram_buffer,
  etna::Buffer& bin_step_size,
  vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;
  auto histogramCalculationInfo = etna::get_shader_program("histogram_calculation");

  auto set = etna::create_descriptor_set(
    histogramCalculationInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, renderTarget.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)},
     etna::Binding{1, current_histogram_buffer.genBinding()},
     etna::Binding{2, bin_step_size.genBinding()}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants(
    pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(binsAmount), &binsAmount);

  etna::flush_barriers(cmd_buf);

  cmd_buf.dispatch((resolution.x + 31) / 32, (resolution.y + 31) / 32, 1);
}

void WorldRenderer::processHistogram(
  vk::CommandBuffer cmd_buf,
  etna::Buffer& current_histogram_buffer,
  vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;
  auto histogramCalculationInfo = etna::get_shader_program("histogram_processing");

  auto set = etna::create_descriptor_set(
    histogramCalculationInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, current_histogram_buffer.genBinding()}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants(
    pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(binsAmount), &binsAmount);

  etna::flush_barriers(cmd_buf);

  cmd_buf.dispatch(1, 1, 1);
}

void WorldRenderer::applyPostprocessing(
  vk::CommandBuffer cmd_buf,
  etna::Buffer& current_histogram_buffer,
  vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;
  auto postprocessingInfo = etna::get_shader_program("postprocess");

  auto set = etna::create_descriptor_set(
    postprocessingInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, renderTarget.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)},
     etna::Binding{1, current_histogram_buffer.genBinding()}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);
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
