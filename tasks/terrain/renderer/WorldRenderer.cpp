#include "WorldRenderer.hpp"
#include "etna/Etna.hpp"
#include "etna/GraphicsPipeline.hpp"
#include "etna/Image.hpp"

#include <cstring>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>
#include <glm/ext.hpp>
#include <vulkan/vulkan_enums.hpp>

WorldRenderer::WorldRenderer()
    : sceneMgr{std::make_unique<SceneManager>()}, maxInstancesInScene{4096},
      instanceMatricesBuffer{std::nullopt} {}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution) {
  resolution = swapchain_resolution;

  auto &ctx = etna::get_context();

  mainViewDepth = ctx.createImage(etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "main_view_depth",
      .format = vk::Format::eD32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eDepthStencilAttachment,
  });

  heightMap = ctx.createImage(etna::Image::CreateInfo{
      .name = "height_map",
      .format = vk::Format::eR32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eSampled |
                    vk::ImageUsageFlagBits::eColorAttachment});

  instanceMatricesBuffer.emplace(
      ctx.getMainWorkCount(),
      [&ctx, maxInstancesInScene = this->maxInstancesInScene](std::size_t i) {
        return ctx.createBuffer(etna::Buffer::CreateInfo{
            .size = sizeof(glm::mat4x4) * maxInstancesInScene,
            .bufferUsage = vk::BufferUsageFlagBits::eVertexBuffer |
                           vk::BufferUsageFlagBits::eTransferDst |
                           vk::BufferUsageFlagBits::eStorageBuffer,
            .memoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU,
            .name = fmt::format("sameInstanceMatrices{}", i)});
      });

  instancesAmount.resize(maxInstancesInScene, 0);
}

void WorldRenderer::loadScene(std::filesystem::path path) {
  sceneMgr->selectBakedScene(path);
}

void WorldRenderer::loadShaders() {
  etna::create_program("static_mesh_material",
                       {TERRAIN_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
                        TERRAIN_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh",
                       {TERRAIN_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("terrain_generator", {})
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format) {
  etna::VertexShaderInputDescription sceneVertexInputDesc{
      .bindings = {etna::VertexShaderInputDescription::Binding{
          .byteStreamDescription = sceneMgr->getVertexFormatDescription(),
      }},
  };

  auto &pipelineManager = etna::get_context().getPipelineManager();

  staticMeshPipeline = {};
  staticMeshPipeline = pipelineManager.createGraphicsPipeline(
      "static_mesh_material",
      etna::GraphicsPipeline::CreateInfo{
          .vertexShaderInput = sceneVertexInputDesc,
          .rasterizationConfig =
              vk::PipelineRasterizationStateCreateInfo{
                  .polygonMode = vk::PolygonMode::eFill,
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

  terrainGenerationPipeline = pipelineManager.createGraphicsPipeline(
      "height_map", etna::GraphicsPipeline::CreateInfo{
                        .fragmentShaderOutput = {
                            .colorAttachmentFormats = {vk::Format::eR32Sfloat},
                        }});
}

void WorldRenderer::debugInput(const Keyboard &) {}

void WorldRenderer::update(const FramePacket &packet) {
  ZoneScoped;

  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
  }
}

void WorldRenderer::renderScene(vk::CommandBuffer cmd_buf,
                                const glm::mat4x4 &glob_tm,
                                vk::PipelineLayout pipeline_layout,
                                etna::Buffer &current_instance_buffer) {
  ZoneScoped;
  if (!sceneMgr->getVertexBuffer())
    return;

  cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0,
                          vk::IndexType::eUint32);

  pushConst2M.projView = glob_tm;

  cmd_buf.pushConstants<PushConstants>(
      pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, {pushConst2M});

  auto shaderInfo = etna::get_shader_program("static_mesh_material");
  auto set = etna::create_descriptor_set(
      shaderInfo.getDescriptorLayoutId(0), cmd_buf,
      {etna::Binding{0, current_instance_buffer.genBinding()}});
  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout,
                             0, 1, &vkSet, 0, nullptr);

  uint32_t offset = 0;

  auto relems = sceneMgr->getRenderElements();

  for (uint32_t i = 0; i < relems.size(); i++) {
    if (instancesAmount[i] > 0) {
      cmd_buf.drawIndexed(relems[i].indexCount, instancesAmount[i],
                          relems[i].indexOffset, relems[i].vertexOffset,
                          offset);
      offset += instancesAmount[i];
    }
  }

  // instancesAmount.clear();
  std::memset(instancesAmount.data(), 0, relems.size() * sizeof(uint32_t));
}

void WorldRenderer::parseInstanceInfo(etna::Buffer &current_buffer,
                                      const glm::mat4x4 &glob_tm) {
  ZoneScoped;

  auto instanceMeshes = sceneMgr->getInstanceMeshes();
  auto instanceMatrices = sceneMgr->getInstanceMatrices();
  auto meshes = sceneMgr->getMeshes();
  auto bounds = sceneMgr->getRenderElementsBounds();

  current_buffer.map();

  glm::mat4x4 *instanceData =
      std::bit_cast<glm::mat4x4 *>(current_buffer.data());

  std::size_t index = 0;
  for (std::size_t i = 0; i < instanceMatrices.size(); i++) {
    const auto meshIdx = instanceMeshes[i];
    const auto &currentMatrix = instanceMatrices[i];

    for (std::size_t j = 0; j < meshes[meshIdx].relemCount; j++) {
      const auto relemIdx = meshes[meshIdx].firstRelem + j;
      if (!isVisible(bounds[relemIdx], glob_tm, currentMatrix)) {
        continue;
      }
      instancesAmount[relemIdx]++;

      instanceData[index] = currentMatrix;
      index++;
    }
  }

  current_buffer.unmap();
}

void WorldRenderer::renderWorld(vk::CommandBuffer cmd_buf,
                                vk::Image target_image,
                                vk::ImageView target_image_view) {
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderForward);

    auto &currentBuffer = instanceMatricesBuffer->get();
    parseInstanceInfo(currentBuffer, worldViewProj);

    etna::RenderTargetState renderTargets(
        cmd_buf, {{0, 0}, {resolution.x, resolution.y}},
        {{.image = target_image, .view = target_image_view}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})});

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics,
                         staticMeshPipeline.getVkPipeline());
    renderScene(cmd_buf, worldViewProj,
                staticMeshPipeline.getVkPipelineLayout(), currentBuffer);
  }
}

bool WorldRenderer::isVisible(const Bounds &bounds, const glm::mat4 &proj_view,
                              const glm::mat4 &transform) {
  std::array corners = {
      glm::vec3{1, 1, 1},   glm::vec3{1, 1, -1},   glm::vec3{1, -1, 1},
      glm::vec3{-1, 1, 1},  glm::vec3{1, -1, -1},  glm::vec3{-1, 1, -1},
      glm::vec3{-1, -1, 1}, glm::vec3{-1, -1, -1},
  };

  glm::vec3 min = {2, 2, 2};    // > 1
  glm::vec3 max = {-2, -2, -2}; // < -1

  auto matrix = proj_view * transform;

  for (const auto &corner : corners) {
    glm::vec4 projection =
        matrix * glm::vec4(bounds.origin + (corner * bounds.extents), 1.0f);

    glm::vec3 current = {projection.x, projection.y, projection.z};
    current /= projection.w;

    min = glm::min(current, min);
    max = glm::max(current, max);
  }

  return min.z <= 1.0f && max.z >= -1.0f && min.x <= 1.0f && max.x >= -1.0f &&
         min.y <= 1.0f && max.y >= -1.0f;
}