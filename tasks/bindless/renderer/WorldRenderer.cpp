#include "WorldRenderer.hpp"

#include <cstddef>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <tracy/Tracy.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "etna/DescriptorSet.hpp"
#include "etna/Etna.hpp"
#include "imgui.h"
#include "stb_image.h"

#include "shaders/postprocessing/UniformHistogramInfo.h"
#include "shaders/MaterialRenderParams.h"


WorldRenderer::WorldRenderer()
  : sceneMgr{std::make_unique<SceneManager>()}
  , renderTargetFormat(vk::Format::eB10G11R11UfloatPack32)
  , maxNumberOfSamples(16)
  // , maxInstancesInScene{4096}
  , binsAmount(128)
  , wireframeEnabled(false)
  , tonemappingEnabled(false)
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

  generationParamsBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(TerrainGenerationParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = fmt::format("generationConstants{}", i)});
  });

  gBuffer.emplace(resolution, renderTargetFormat);

  params.terrainInChunks = shader_uvec2(64, 64);
  params.terrainOffset = shader_vec2(0, 0);
  params.chunk = shader_uvec2(16, 16);

  // instanceMatricesBuffer.emplace(
  //   ctx.getMainWorkCount(), [&ctx, maxInstancesInScene = this->maxInstancesInScene](std::size_t
  //   i) {
  //     return ctx.createBuffer(etna::Buffer::CreateInfo{
  //       .size = sizeof(glm::mat4x4) * maxInstancesInScene,
  //       .bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer |
  //         vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
  //       .memoryUsage = VMA_MEMORY_USAGE_AUTO,
  //       .allocationCreate =
  //         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
  //         VMA_ALLOCATION_CREATE_MAPPED_BIT,
  //       .name = fmt::format("sameInstanceMatrices{}", i)});
  //   });

  constantsBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(UniformParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO,
      .allocationCreate =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
      .name = fmt::format("constants{}", i)});
  });

  histogramBuffer.emplace(
    ctx.getMainWorkCount(), [&ctx, binsAmount = this->binsAmount](std::size_t i) {
      return ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = binsAmount * sizeof(int32_t),
        .bufferUsage =
          vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .name = fmt::format("histogram{}", i)});
    });

  histogramInfoBuffer.emplace(ctx.getMainWorkCount(), [&ctx](std::size_t i) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(UniformHistogramInfo),
      .bufferUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = fmt::format("histogram_info{}", i)});
  });

  distributionBuffer.emplace(
    ctx.getMainWorkCount(), [&ctx, binsAmount = this->binsAmount](std::size_t i) {
      return ctx.createBuffer(etna::Buffer::CreateInfo{
        .size = binsAmount * sizeof(float),
        .bufferUsage =
          vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
        .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .name = fmt::format("distribution{}", i)});
    });

  terrainSampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "terrain_sampler"});
  staticMeshSampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "static_mesh_sampler"});

  oneShotCommands = ctx.createOneShotCmdMgr();

  transferHelper = std::make_unique<etna::BlockingTransferHelper>(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 6});

  // instancesAmount.resize(maxInstancesInScene, 0);
}

// call only after loadShaders(...)
void WorldRenderer::loadScene(std::filesystem::path path)
{
  sceneMgr->selectBakedScene(path);

  auto shaderInfo = etna::get_shader_program("static_mesh_material");
  meshesDescriptorSet = etna::create_persistent_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0), sceneMgr->getBindlessBindings(), true);

  params.instancesCount = sceneMgr->getInstanceMeshes().size();
  params.relemsCount = sceneMgr->getRenderElements().size();
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "static_mesh_material",
    {BINDLESS_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     BINDLESS_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh", {BINDLESS_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program(
    "terrain_generator",
    {BINDLESS_RENDERER_SHADERS_ROOT "decoy.vert.spv",
     BINDLESS_RENDERER_SHADERS_ROOT "generator.frag.spv"});

  etna::create_program("culling_meshes", {BINDLESS_RENDERER_SHADERS_ROOT "culling.comp.spv"});

  etna::create_program(
    "terrain_normal_map_calculation", {BINDLESS_RENDERER_SHADERS_ROOT "calculate_normal.comp.spv"});

  etna::create_program(
    "terrain_render",
    {BINDLESS_RENDERER_SHADERS_ROOT "chunk.vert.spv",
     BINDLESS_RENDERER_SHADERS_ROOT "subdivide_chunk.tesc.spv",
     BINDLESS_RENDERER_SHADERS_ROOT "process_chunk.tese.spv",
     BINDLESS_RENDERER_SHADERS_ROOT "terrain.frag.spv"});

  etna::create_program(
    "lights_displacement", {BINDLESS_RENDERER_SHADERS_ROOT "displace_lights.comp.spv"});

  etna::create_program(
    "cubemap_render",
    {BINDLESS_RENDERER_SHADERS_ROOT "skybox.vert.spv",
     BINDLESS_RENDERER_SHADERS_ROOT "skybox.frag.spv"});
  etna::create_program(
    "deferred_shading",
    {BINDLESS_RENDERER_SHADERS_ROOT "decoy.vert.spv",
     BINDLESS_RENDERER_SHADERS_ROOT "shading.frag.spv"});

  etna::create_program(
    "min_max_calculation", {BINDLESS_RENDERER_SHADERS_ROOT "calculate_min_max.comp.spv"});
  etna::create_program(
    "histogram_calculation", {BINDLESS_RENDERER_SHADERS_ROOT "histogram.comp.spv"});
  etna::create_program(
    "histogram_processing", {BINDLESS_RENDERER_SHADERS_ROOT "process_histogram.comp.spv"});
  etna::create_program(
    "postprocess_compute", {BINDLESS_RENDERER_SHADERS_ROOT "postprocess.comp.spv"});
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
      .blendingConfig =
        {
          .attachments =
            {{
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             },
             {
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             },
             {
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             }},
          .logicOpEnable = false,
          .logicOp = {},
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats =
            {renderTargetFormat, vk::Format::eR8G8B8A8Snorm, vk::Format::eR8G8B8A8Unorm},
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
      .blendingConfig =
        {
          .attachments =
            {{
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             },
             {
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             },
             {
               .blendEnable = vk::False,
               .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
             }},
          .logicOpEnable = false,
          .logicOp = {},
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats =
            {renderTargetFormat, vk::Format::eR8G8B8A8Snorm, vk::Format::eR8G8B8A8Unorm},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  cubemapRenderPipeline = pipelineManager.createGraphicsPipeline(
    "cubemap_render",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {renderTargetFormat},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

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

  cullingPipeline = pipelineManager.createComputePipeline("culling_meshes", {});

  calculateMinMaxPipeline = pipelineManager.createComputePipeline("min_max_calculation", {});
  histogramPipeline = pipelineManager.createComputePipeline("histogram_calculation", {});
  processHistogramPipeline = pipelineManager.createComputePipeline("histogram_processing", {});
  postprocessComputePipeline = pipelineManager.createComputePipeline("postprocess_compute", {});
}

void WorldRenderer::rebuildRenderPipelines()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getQueue().waitIdle());

  // TODO: fix sync error in queue submit
  setupRenderPipelines();
}

void WorldRenderer::setupTerrainGeneration(vk::Format texture_format, vk::Extent3D extent)
{
  auto& ctx = etna::get_context();
  auto& pipelineManager = ctx.getPipelineManager();

  terrainMap = ctx.createImage(etna::Image::CreateInfo{
    .extent = extent,
    .name = "terrain_map",
    .format = texture_format,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment |
      vk::ImageUsageFlagBits::eStorage});
  terrainNormalMap = ctx.createImage(etna::Image::CreateInfo{
    .extent = extent,
    .name = "terrain_normal_map",
    .format = vk::Format::eR8G8B8A8Snorm,
    .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage});

  terrainGenerationPipeline = pipelineManager.createGraphicsPipeline(
    "terrain_generator",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {
        .colorAttachmentFormats = {texture_format},
      }});

  terrainNormalPipeline =
    pipelineManager.createComputePipeline("terrain_normal_map_calculation", {});

  lightDisplacementPipeline = pipelineManager.createComputePipeline("lights_displacement", {});

  params.extent = shader_uvec2(extent.width, extent.height);
  params.heightAmplifier = 200.0f;
  params.heightOffset = 0.6f;
  generationParams = {.extent = params.extent, .numberOfSamples = 3, .persistence = 0.5};
}

void WorldRenderer::loadLights()
{
  auto& ctx = etna::get_context();

  params.constant = 1.0f;
  params.linear = 0.14f;
  params.quadratic = 0.07f;

  lights = {
    Light{.pos = {0, 27, 0}, .radius = 0, .worldPos = {}, .color = {1, 1, 1}, .intensity = 15},
    Light{.pos = {0, 5, 0}, .radius = 0, .worldPos = {}, .color = {1, 0, 1}, .intensity = 15},
    Light{.pos = {0, 5, 25}, .radius = 0, .worldPos = {}, .color = {1, 1, 1}, .intensity = 15},
    Light{.pos = {3, 5, 50}, .radius = 0, .worldPos = {}, .color = {0.5, 1, 0.5}, .intensity = 15},
    Light{.pos = {75, 5, 75}, .radius = 0, .worldPos = {}, .color = {1, 0.5, 1}, .intensity = 15},
    Light{.pos = {50, 5, 20}, .radius = 0, .worldPos = {}, .color = {0, 1, 1}, .intensity = 15},
    Light{.pos = {25, 5, 50}, .radius = 0, .worldPos = {}, .color = {1, 1, 0}, .intensity = 15},
    Light{.pos = {50, 5, 50}, .radius = 0, .worldPos = {}, .color = {0.3, 1, 0}, .intensity = 15},
    Light{.pos = {25, 5, 10}, .radius = 0, .worldPos = {}, .color = {1, 1, 0}, .intensity = 15},
    Light{
      .pos = {100, 5, 100}, .radius = 0, .worldPos = {}, .color = {1, 0.5, 0.5}, .intensity = 15},
    Light{.pos = {150, 5, 150}, .radius = 0, .worldPos = {}, .color = {1, 1, 1}, .intensity = 100},
    Light{.pos = {25, 5, 10}, .radius = 0, .worldPos = {}, .color = {1, 1, 0}, .intensity = 15},
    Light{.pos = {10, 5, 25}, .radius = 0, .worldPos = {}, .color = {1, 0, 1}, .intensity = 15}};

  for (auto& light : lights)
  {
    float lightMax = glm::max(light.color.r, light.color.g, light.color.b);
    light.radius = (-params.linear +
                    static_cast<float>(glm::sqrt(
                      params.linear * params.linear -
                      4 * params.quadratic * (params.constant - (256.0 / 5.0) * lightMax)))) /
      (2 * params.quadratic);
    // spdlog::info("radius - {}", light.radius);
  }

  directionalLights = {DirectionalLight{
    .direction = glm::vec3{-1, -1, 0.5},
    .intensity = 1.0f,
    .color = glm::normalize(glm::vec3{251, 172, 19})}};

  vk::DeviceSize directionalLightsSize = sizeof(DirectionalLight) * directionalLights.size();
  vk::DeviceSize lightsSize = sizeof(Light) * lights.size();

  directionalLightsBuffer = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = directionalLightsSize,
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = fmt::format("DirectionalLights")});
  lightsBuffer = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = lightsSize,
    .bufferUsage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    .name = fmt::format("Lights")});

  transferHelper->uploadBuffer(
    *oneShotCommands, directionalLightsBuffer, 0, std::as_bytes(std::span(directionalLights)));
  transferHelper->uploadBuffer(*oneShotCommands, lightsBuffer, 0, std::as_bytes(std::span(lights)));

  params.directionalLightsAmount = static_cast<uint32_t>(directionalLights.size());
  params.lightsAmount = static_cast<uint32_t>(lights.size());
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

  sceneMgr->localCopyBufferToImage(cubemapBuffer, cubemapTexture, layerCount);

  sceneMgr->generateMipmapsVkStyle(cubemapTexture, mipLevels, layerCount);

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
  }
}

void WorldRenderer::drawGui()
{
  static ImU32 numberOfSamplesMin = 1;
  static ImU32 numberOfSamplesMax = maxNumberOfSamples;
  static float persistenceMin = 0.0f;
  static float persistenceMax = 1.0f;
  ImGui::Begin("Render Settings");

  if (ImGui::CollapsingHeader("Lights"))
  {
    static bool directionalLightsChanged = false;
    static bool lightsChanged = false;
    ImGuiColorEditFlags colorFlags =
      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoAlpha;
    ImGui::SeparatorText("Directional Lights");
    for (uint32_t i = 0; i < directionalLights.size(); i++)
    {
      auto& currentLight = directionalLights[i];
      if (ImGui::TreeNode(&currentLight, "Light %d", i))
      {
        float direction[] = {
          currentLight.direction.x, currentLight.direction.y, currentLight.direction.z};
        float color[] = {
          currentLight.color.r,
          currentLight.color.g,
          currentLight.color.b,
        };
        float intensity = currentLight.intensity;
        directionalLightsChanged =
          directionalLightsChanged || ImGui::DragFloat3("Direction angles", direction);
        currentLight.direction = shader_vec3(direction[0], direction[1], direction[2]);
        directionalLightsChanged =
          directionalLightsChanged || ImGui::ColorEdit3("Color", color, colorFlags);
        currentLight.color = shader_vec3(color[0], color[1], color[2]);
        directionalLightsChanged =
          directionalLightsChanged || ImGui::DragFloat("Intensity", &intensity);
        currentLight.intensity = intensity;

        ImGui::TreePop();
      }
    }
    ImGui::SeparatorText("Point Lights");
    for (uint32_t i = 0; i < lights.size(); i++)
    {
      auto& currentLight = lights[i];
      if (ImGui::TreeNode(&currentLight, "Light %d", i))
      {

        float position[] = {currentLight.pos.x, currentLight.pos.y, currentLight.pos.z};
        float color[] = {
          currentLight.color.r,
          currentLight.color.g,
          currentLight.color.b,
        };
        float radius = currentLight.radius;
        float intensity = currentLight.intensity;
        lightsChanged = lightsChanged || ImGui::DragFloat3("Position", position);
        currentLight.pos = shader_vec3(position[0], position[1], position[2]);
        lightsChanged = lightsChanged || ImGui::ColorEdit3("Color", color, colorFlags);
        currentLight.color = shader_vec3(color[0], color[1], color[2]);
        lightsChanged = lightsChanged || ImGui::DragFloat("Radius", &radius);
        currentLight.radius = radius;
        lightsChanged = lightsChanged || ImGui::DragFloat("Intensity", &intensity);
        currentLight.intensity = intensity;

        ImGui::TreePop();
      }
    }

    if (directionalLightsChanged)
    {
      transferHelper->uploadBuffer(
        *oneShotCommands, directionalLightsBuffer, 0, std::as_bytes(std::span(directionalLights)));
      directionalLightsChanged = false;
    }
    if (lightsChanged)
    {
      transferHelper->uploadBuffer(
        *oneShotCommands, lightsBuffer, 0, std::as_bytes(std::span(lights)));
      displaceLights();
      lightsChanged = false;
    }
  }

  if (ImGui::CollapsingHeader("Terrain Generation"))
  {
    ImGui::SeparatorText("Generation parameters");
    ImGui::SliderScalar(
      "Number of samples",
      ImGuiDataType_U32,
      &generationParams.numberOfSamples,
      &numberOfSamplesMin,
      &numberOfSamplesMax,
      "%u");
    ImGui::SliderScalar(
      "Persistence",
      ImGuiDataType_Float,
      &generationParams.persistence,
      &persistenceMin,
      &persistenceMax,
      "%f");
    if (ImGui::Button("Regenerate Terrain"))
    {
      generateTerrain();
    }
  }

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

void WorldRenderer::generateTerrain()
{
  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    auto& currentConstants = constantsBuffer->get();
    updateConstants(currentConstants);

    auto& currentGenerationConstants = generationParamsBuffer->get();
    currentGenerationConstants.map();
    std::memcpy(
      currentGenerationConstants.data(), &generationParams, sizeof(TerrainGenerationParams));
    currentGenerationConstants.unmap();

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eColorAttachmentOutput,
      vk::AccessFlagBits2::eColorAttachmentWrite,
      vk::ImageLayout::eColorAttachmentOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    auto extent = terrainMap.getExtent();
    glm::uvec2 glmExtent = {extent.width, extent.height};

    {
      etna::RenderTargetState state(
        commandBuffer,
        {{}, {glmExtent.x, glmExtent.y}},
        {{terrainMap.get(), terrainMap.getView({})}},
        {});

      auto shaderInfo = etna::get_shader_program("terrain_generator");
      auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        commandBuffer,
        {etna::Binding{0, currentGenerationConstants.genBinding()}});

      auto vkSet = set.getVkSet();

      commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        terrainGenerationPipeline.getVkPipelineLayout(),
        0,
        1,
        &vkSet,
        0,
        nullptr);


      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eGraphics, terrainGenerationPipeline.getVkPipeline());

      commandBuffer.draw(3, 1, 0, 0);
    }

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageRead,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      commandBuffer,
      terrainNormalMap.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    {
      auto shaderInfo = etna::get_shader_program("terrain_normal_map_calculation");

      auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        commandBuffer,
        {etna::Binding{0, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)},
         etna::Binding{
           1, terrainNormalMap.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)}});

      auto vkSet = set.getVkSet();

      commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        terrainNormalPipeline.getVkPipelineLayout(),
        0,
        1,
        &vkSet,
        0,
        nullptr);

      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eCompute, terrainNormalPipeline.getVkPipeline());

      commandBuffer.pushConstants<glm::uvec2>(
        terrainNormalPipeline.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eCompute,
        0,
        {params.chunk});

      commandBuffer.dispatch((glmExtent.x + 31) / 32, (glmExtent.y + 31) / 32, 1);
    }
    {
      std::array bufferBarriers = {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .buffer = lightsBuffer.get(),
        .size = vk::WholeSize}};

      vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
        .pBufferMemoryBarriers = bufferBarriers.data()};

      commandBuffer.pipelineBarrier2(dependencyInfo);
    }
    {
      auto shaderInfo = etna::get_shader_program("lights_displacement");

      auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        commandBuffer,
        {etna::Binding{0, currentConstants.genBinding()},
         etna::Binding{1, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)},
         etna::Binding{2, lightsBuffer.genBinding()}});

      auto vkSet = set.getVkSet();

      commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        lightDisplacementPipeline.getVkPipelineLayout(),
        0,
        1,
        &vkSet,
        0,
        nullptr);

      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eCompute, lightDisplacementPipeline.getVkPipeline());

      commandBuffer.dispatch(1, 1, 1);
    }

    {
      std::array bufferBarriers = {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .buffer = lightsBuffer.get(),
        .size = vk::WholeSize}};

      vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
        .pBufferMemoryBarriers = bufferBarriers.data()};

      commandBuffer.pipelineBarrier2(dependencyInfo);
    }

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      commandBuffer,
      terrainNormalMap.get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void WorldRenderer::displaceLights()
{
  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    auto& currentConstants = constantsBuffer->get();
    updateConstants(currentConstants);

    auto& currentGenerationConstants = generationParamsBuffer->get();
    currentGenerationConstants.map();
    std::memcpy(
      currentGenerationConstants.data(), &generationParams, sizeof(TerrainGenerationParams));
    currentGenerationConstants.unmap();

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageRead,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      commandBuffer,
      terrainNormalMap.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageWrite,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);

    {
      std::array bufferBarriers = {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .buffer = lightsBuffer.get(),
        .size = vk::WholeSize}};

      vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
        .pBufferMemoryBarriers = bufferBarriers.data()};

      commandBuffer.pipelineBarrier2(dependencyInfo);
    }
    {
      auto shaderInfo = etna::get_shader_program("lights_displacement");

      auto set = etna::create_descriptor_set(
        shaderInfo.getDescriptorLayoutId(0),
        commandBuffer,
        {etna::Binding{0, currentConstants.genBinding()},
         etna::Binding{1, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eGeneral)},
         etna::Binding{2, lightsBuffer.genBinding()}});

      auto vkSet = set.getVkSet();

      commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        lightDisplacementPipeline.getVkPipelineLayout(),
        0,
        1,
        &vkSet,
        0,
        nullptr);

      commandBuffer.bindPipeline(
        vk::PipelineBindPoint::eCompute, lightDisplacementPipeline.getVkPipeline());

      commandBuffer.dispatch(1, 1, 1);
    }

    {
      std::array bufferBarriers = {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .buffer = lightsBuffer.get(),
        .size = vk::WholeSize}};

      vk::DependencyInfo dependencyInfo = {
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
        .pBufferMemoryBarriers = bufferBarriers.data()};

      commandBuffer.pipelineBarrier2(dependencyInfo);
    }

    etna::set_state(
      commandBuffer,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      commandBuffer,
      terrainNormalMap.get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

void WorldRenderer::cullMeshes(
  vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;
  {
    std::array bufferBarriers = {
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .buffer = sceneMgr->getDrawInstanceIndicesBuffer().get(),
        .size = vk::WholeSize},
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
        .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .buffer = sceneMgr->getDrawCommandsBuffer().get(),
        .size = vk::WholeSize}};

    vk::DependencyInfo dependencyInfo = {
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()};

    cmd_buf.pipelineBarrier2(dependencyInfo);
  }

  auto shaderInfo = etna::get_shader_program("culling_meshes");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, sceneMgr->getRelemsBuffer().genBinding()},
     etna::Binding{1, sceneMgr->getBoundsBuffer().genBinding()},
     etna::Binding{2, sceneMgr->getMeshesBuffer().genBinding()},
     etna::Binding{3, sceneMgr->getInstanceMeshesBuffer().genBinding()},
     etna::Binding{4, sceneMgr->getInstanceMatricesBuffer().genBinding()},
     etna::Binding{5, sceneMgr->getRelemInstanceOffsetsBuffer().genBinding()},
     etna::Binding{6, sceneMgr->getDrawInstanceIndicesBuffer().genBinding()},
     etna::Binding{7, sceneMgr->getDrawCommandsBuffer().genBinding()},
     etna::Binding{8, constants.genBinding()}});
  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.dispatch((sceneMgr->getInstanceMeshes().size() + 127) / 128, 1, 1);

  {
    std::array bufferBarriers = {
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
        .buffer = sceneMgr->getDrawInstanceIndicesBuffer().get(),
        .size = vk::WholeSize},
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
        .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
        .buffer = sceneMgr->getDrawCommandsBuffer().get(),
        .size = vk::WholeSize}};

    vk::DependencyInfo dependencyInfo = {
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
      .pBufferMemoryBarriers = bufferBarriers.data()};

    cmd_buf.pipelineBarrier2(dependencyInfo);
  }
}

void WorldRenderer::renderScene(
  vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout)
// ,  etna::Buffer& instance_buffer)
{
  ZoneScoped;
  if (!sceneMgr->getVertexBuffer())
    return;

  cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  auto shaderInfo = etna::get_shader_program("static_mesh_material");

  // set 0 is persistent, for materials and textures
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(1),
    cmd_buf,
    {etna::Binding{0, sceneMgr->getRelemsBuffer().genBinding()},
     etna::Binding{1, sceneMgr->getInstanceMatricesBuffer().genBinding()},
     etna::Binding{2, sceneMgr->getDrawInstanceIndicesBuffer().genBinding()},
     etna::Binding{3, constants.genBinding()}});

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics,
    pipeline_layout,
    0,
    {meshesDescriptorSet->getVkSet(), set.getVkSet()},
    {});

  cmd_buf.drawIndexedIndirect(
    sceneMgr->getDrawCommandsBuffer().get(),
    0,
    sceneMgr->getRenderElements().size(),
    sizeof(vk::DrawIndexedIndirectCommand));

  // uint32_t offset = 0;

  // auto relems = sceneMgr->getRenderElements();


  // for (uint32_t i = 0; i < relems.size(); i++)
  // {
  //   if (instancesAmount[i] > 0)
  //   {
  //     auto& material = sceneMgr->getMaterial(
  //       (relems[i].material == Material::Id::Invalid ? sceneMgr->materialPlaceholder
  //                                                    : relems[i].material));

  //     auto& baseColorTexture = sceneMgr->getTexture(material.baseColorTexture).texture;
  //     auto& metallicRoughnessTexture =
  //       sceneMgr->getTexture(material.metallicRoughnessTexture).texture;
  //     auto& normalTexture = sceneMgr->getTexture(material.normalTexture).texture;

  //     auto set = etna::create_descriptor_set(
  //       shaderInfo.getDescriptorLayoutId(0),
  //       cmd_buf,
  //       {etna::Binding{0, instance_buffer.genBinding()},
  //        etna::Binding{1, constants.genBinding()},
  //        etna::Binding{
  //          2,
  //          baseColorTexture.genBinding(
  //            staticMeshSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
  //        etna::Binding{
  //          3,
  //          normalTexture.genBinding(
  //            staticMeshSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
  //        etna::Binding{
  //          4,
  //          metallicRoughnessTexture.genBinding(
  //            staticMeshSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});
  //     auto vkSet = set.getVkSet();

  //     cmd_buf.bindDescriptorSets(
  //       vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  //     MaterialRenderParams materialParams = {
  //       .baseColorFactor = material.baseColorFactor,
  //       .roughnessFactor = material.roughnessFactor,
  //       .metallicFactor = material.metallicFactor};

  //     cmd_buf.pushConstants<MaterialRenderParams>(
  //       pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, {materialParams});

  //     cmd_buf.drawIndexed(
  //       relems[i].indexCount,
  //       instancesAmount[i],
  //       relems[i].indexOffset,
  //       relems[i].vertexOffset,
  //       offset);
  //     offset += instancesAmount[i];
  //   }
  // }

  // // instancesAmount.clear();
  // std::memset(instancesAmount.data(), 0, relems.size() * sizeof(uint32_t));
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
       1, terrainMap.genBinding(terrainSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
     etna::Binding{
       2,
       terrainNormalMap.genBinding(
         terrainSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.draw(4, params.terrainInChunks.x * params.terrainInChunks.y, 0, 0);
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
     etna::Binding{5, lightsBuffer.genBinding()},
     etna::Binding{6, directionalLightsBuffer.genBinding()},
     etna::Binding{
       7,
       cubemapTexture.genBinding(
         staticMeshSampler.get(),
         vk::ImageLayout::eShaderReadOnlyOptimal,
         {.type = vk::ImageViewType::eCube})}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants(
    pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(glm::uvec2), &resolution);

  cmd_buf.draw(3, 1, 0, 0);
}

void WorldRenderer::renderCubemap(
  vk::CommandBuffer cmd_buf, etna::Buffer& constants, vk::PipelineLayout pipeline_layout)
{
  ZoneScoped;

  auto shaderInfo = etna::get_shader_program("cubemap_render");
  auto set = etna::create_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, constants.genBinding()},
     etna::Binding{
       1,
       cubemapTexture.genBinding(
         staticMeshSampler.get(),
         vk::ImageLayout::eShaderReadOnlyOptimal,
         {.type = vk::ImageViewType::eCube})}});

  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants(
    pipeline_layout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(glm::uvec2), &resolution);

  cmd_buf.draw(3, 1, 0, 0);
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

void WorldRenderer::renderWorld(vk::CommandBuffer cmd_buf, vk::Image target_image)
// [[maybe_unused]] vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderDeferred);

    auto& currentConstants = constantsBuffer->get();
    updateConstants(currentConstants);

    cullMeshes(cmd_buf, currentConstants, cullingPipeline.getVkPipelineLayout());

    // auto& currentBuffer = instanceMatricesBuffer->get();
    // parseInstanceInfo(currentBuffer);

    // {
    //   ETNA_PROFILE_GPU(cmd_buf, renderTerrain);
    //   etna::RenderTargetState renderTargets(
    //     cmd_buf,
    //     {{0, 0}, {resolution.x, resolution.y}},
    //     {{.image = renderTarget.get(), .view = renderTarget.getView({})}},
    //     {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

    //   cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
    //   terrainRenderPipeline.getVkPipeline()); renderTerrain(cmd_buf, currentConstants,
    //   terrainRenderPipeline.getVkPipelineLayout());
    // }
    etna::set_state(
      cmd_buf,
      terrainMap.get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    gBuffer->prepareForRender(cmd_buf);

    etna::flush_barriers(cmd_buf);

    {
      ETNA_PROFILE_GPU(cmd_buf, renderTerrain);
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        gBuffer->genColorAttachmentParams(),
        gBuffer->genDepthAttachmentParams());

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, terrainRenderPipeline.getVkPipeline());
      renderTerrain(cmd_buf, currentConstants, terrainRenderPipeline.getVkPipelineLayout());
    }

    {
      ETNA_PROFILE_GPU(cmd_buf, renderMeshes);
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        gBuffer->genColorAttachmentParams(vk::AttachmentLoadOp::eLoad),
        gBuffer->genDepthAttachmentParams(vk::AttachmentLoadOp::eLoad));

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());
      renderScene(
        cmd_buf, currentConstants, staticMeshPipeline.getVkPipelineLayout()); //, currentBuffer);
    }

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
      terrainMap.get(),
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
        {{.image = renderTarget.get(),
          .view = renderTarget.getView({}),
          .loadOp = vk::AttachmentLoadOp::eLoad}},
        {});

      cmd_buf.bindPipeline(
        vk::PipelineBindPoint::eGraphics, deferredShadingPipeline.getVkPipeline());
      deferredShading(cmd_buf, currentConstants, deferredShadingPipeline.getVkPipelineLayout());
    }

    gBuffer->continueDepthWrite(cmd_buf);
    etna::flush_barriers(cmd_buf);

    {
      ETNA_PROFILE_GPU(cmd_buf, renderSkybox);
      etna::RenderTargetState renderTargets(
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = renderTarget.get(),
          .view = renderTarget.getView({}),
          .loadOp = vk::AttachmentLoadOp::eLoad}},
        gBuffer->genDepthAttachmentParams(vk::AttachmentLoadOp::eLoad));

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, cubemapRenderPipeline.getVkPipeline());
      renderCubemap(cmd_buf, currentConstants, cubemapRenderPipeline.getVkPipelineLayout());
    }


    if (tonemappingEnabled)
    {
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

// void WorldRenderer::parseInstanceInfo(etna::Buffer& buffer)
// {
//   ZoneScoped;

//   auto instanceMeshes = sceneMgr->getInstanceMeshes();
//   auto instanceMatrices = sceneMgr->getInstanceMatrices();
//   auto meshes = sceneMgr->getMeshes();
//   auto bounds = sceneMgr->getRenderElementsBounds();

//   buffer.map();

//   glm::mat4x4* instanceData = std::bit_cast<glm::mat4x4*>(buffer.data());

//   std::size_t index = 0;
//   for (std::size_t i = 0; i < instanceMatrices.size(); i++)
//   {
//     const auto meshIdx = instanceMeshes[i];
//     const auto& currentMatrix = instanceMatrices[i];

//     for (std::size_t j = 0; j < meshes[meshIdx].relemCount; j++)
//     {
//       const auto relemIdx = meshes[meshIdx].firstRelem + j;
//       if (!isVisible(bounds[relemIdx], params.projView, currentMatrix))
//       {
//         continue;
//       }
//       instancesAmount[relemIdx]++;

//       instanceData[index] = currentMatrix;
//       index++;
//     }
//   }

//   buffer.unmap();
// }

void WorldRenderer::updateConstants(etna::Buffer& constants)
{
  ZoneScoped;

  constants.map();

  std::memcpy(constants.data(), &params, sizeof(UniformParams));

  constants.unmap();
}

// bool WorldRenderer::isVisible(
//   const Bounds& bounds, const glm::mat4& proj_view, const glm::mat4& transform)
// {
//   std::array corners = {
//     glm::vec3{1, 1, 1},
//     glm::vec3{1, 1, -1},
//     glm::vec3{1, -1, 1},
//     glm::vec3{-1, 1, 1},
//     glm::vec3{1, -1, -1},
//     glm::vec3{-1, 1, -1},
//     glm::vec3{-1, -1, 1},
//     glm::vec3{-1, -1, -1},
//   };

//   glm::vec3 min = {2, 2, 2};    // > 1
//   glm::vec3 max = {-2, -2, -2}; // < -1

//   auto matrix = proj_view * transform;

//   for (const auto& corner : corners)
//   {
//     glm::vec4 projection = matrix * glm::vec4(bounds.origin + (corner * bounds.extents), 1.0f);

//     glm::vec3 current = {projection.x, projection.y, projection.z};
//     current /= projection.w;

//     min = glm::min(current, min);
//     max = glm::max(current, max);
//   }

//   return min.z <= 1.0f && max.z >= -1.0f && min.x <= 1.0f && max.x >= -1.0f && min.y <= 1.0f &&
//     max.y >= -1.0f;
// }
