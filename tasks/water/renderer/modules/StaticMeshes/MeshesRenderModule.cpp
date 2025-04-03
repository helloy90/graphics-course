#include "MeshesRenderModule.hpp"

#include <tracy/Tracy.hpp>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>
#include <etna/RenderTargetStates.hpp>

#include "shaders/MeshesParams.h"


MeshesRenderModule::MeshesRenderModule()
  : sceneMgr{std::make_unique<SceneManager>()}
{
}

void MeshesRenderModule::allocateResources()
{
  paramsBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(MeshesParams),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO,
    .allocationCreate =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .name = "meshesParams"});

  staticMeshSampler = etna::Sampler(
    etna::Sampler::CreateInfo{.filter = vk::Filter::eLinear, .name = "static_mesh_sampler"});
  oneShotCommands = etna::get_context().createOneShotCmdMgr();

  transferHelper = std::make_unique<etna::BlockingTransferHelper>(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = sizeof(MeshesParams)});
}

void MeshesRenderModule::loadShaders()
{
  etna::create_program(
    "static_mesh_material",
    {STATIC_MESHES_SHADERS_ROOT "static_mesh.frag.spv",
     STATIC_MESHES_SHADERS_ROOT "static_mesh.vert.spv"});

  etna::create_program("culling_meshes", {STATIC_MESHES_SHADERS_ROOT "culling.comp.spv"});
}

void MeshesRenderModule::loadScene(std::filesystem::path path)
{
  sceneMgr->selectBakedScene(path);

  auto shaderInfo = etna::get_shader_program("static_mesh_material");
  meshesDescriptorSet = etna::create_persistent_descriptor_set(
    shaderInfo.getDescriptorLayoutId(0), sceneMgr->getBindlessBindings(), true);

  auto commandBuffer = oneShotCommands->start();
  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    meshesDescriptorSet->processBarriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());
  oneShotCommands->submitAndWait(commandBuffer);

  params.instancesCount = sceneMgr->getInstanceMeshes().size();
  params.relemsCount = sceneMgr->getRenderElements().size();

  paramsBuffer.map();
  std::memcpy(paramsBuffer.data(), &params, sizeof(MeshesParams));
  paramsBuffer.unmap();
}

void MeshesRenderModule::setupPipelines(bool wireframe_enabled, vk::Format render_target_format)
{
  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = sceneMgr->getVertexFormatDescription(),
    }},
  };

  auto& pipelineManager = etna::get_context().getPipelineManager();

  staticMeshPipeline = pipelineManager.createGraphicsPipeline(
    "static_mesh_material",
    etna::GraphicsPipeline::CreateInfo{
      .vertexShaderInput = sceneVertexInputDesc,
      .rasterizationConfig =
        vk::PipelineRasterizationStateCreateInfo{
          .polygonMode = (wireframe_enabled ? vk::PolygonMode::eLine : vk::PolygonMode::eFill),
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
            {render_target_format, vk::Format::eR8G8B8A8Snorm, vk::Format::eR8G8B8A8Unorm},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    });

  cullingPipeline = pipelineManager.createComputePipeline("culling_meshes", {});
}

void MeshesRenderModule::execute(
  vk::CommandBuffer cmd_buf,
  const glm::mat4x4& proj_view,
  glm::uvec2 extent,
  std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
  etna::RenderTargetState::AttachmentParams depth_attachment_params)
{
  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipeline());
  cullMeshes(cmd_buf, cullingPipeline.getVkPipelineLayout(), proj_view);

  {
    ETNA_PROFILE_GPU(cmd_buf, renderScene);
    etna::RenderTargetState renderTargets(
      cmd_buf, {{0, 0}, {extent.x, extent.y}}, color_attachment_params, depth_attachment_params);

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());
    renderScene(cmd_buf, staticMeshPipeline.getVkPipelineLayout(), proj_view);
  }
}

void MeshesRenderModule::cullMeshes(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, const glm::mat4x4& proj_view)
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
     etna::Binding{8, paramsBuffer.genBinding()}});
  auto vkSet = set.getVkSet();

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline_layout, 0, 1, &vkSet, 0, nullptr);

  cmd_buf.pushConstants<glm::mat4x4>(
    pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, {proj_view});

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

void MeshesRenderModule::renderScene(
  vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, const glm::mat4x4& proj_view)
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
     etna::Binding{2, sceneMgr->getDrawInstanceIndicesBuffer().genBinding()}});

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics,
    pipeline_layout,
    0,
    {meshesDescriptorSet->getVkSet(), set.getVkSet()},
    {});

  cmd_buf.pushConstants<glm::mat4x4>(
    pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, {proj_view});

  cmd_buf.drawIndexedIndirect(
    sceneMgr->getDrawCommandsBuffer().get(),
    0,
    sceneMgr->getRenderElements().size(),
    sizeof(vk::DrawIndexedIndirectCommand));
}
