#pragma once

#include <etna/Buffer.hpp>
#include <etna/GpuSharedResource.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/DescriptorSet.hpp>

#include <glm/glm.hpp>

#include "etna/RenderTargetStates.hpp"
#include "etna/Sampler.hpp"
#include "scene/SceneManager.hpp"

#include "../Module.hpp"
#include "../RenderPacket.hpp"
#include "shaders/MeshesParams.h"


class MeshesRenderModule
{
public:
  MeshesRenderModule();

  void allocateResources();
  void loadShaders();
  void loadScene(std::filesystem::path path);
  void setupPipelines(bool wireframe_enabled, vk::Format render_target_format);
  void execute(
    vk::CommandBuffer cmd_buf,
    const RenderPacket& packet,
    glm::uvec2 extent,
    std::vector<etna::RenderTargetState::AttachmentParams> color_attachment_params,
    etna::RenderTargetState::AttachmentParams depth_attachment_params);

  void drawGui();

  const etna::Sampler& getStaticMeshSampler() { return staticMeshSampler; }

private:
  void cullMeshes(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, const glm::mat4x4& proj_view);

  void renderScene(
    vk::CommandBuffer cmd_buf, vk::PipelineLayout pipeline_layout, const glm::mat4x4& proj_view);

private:
  MeshesParams params;
  etna::Buffer paramsBuffer;

  std::unique_ptr<SceneManager> sceneMgr;

  std::optional<etna::PersistentDescriptorSet> meshesDescriptorSet;

  etna::GraphicsPipeline staticMeshPipeline;
  etna::ComputePipeline cullingPipeline;

  etna::Sampler staticMeshSampler;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;
};

// static_assert(Module<MeshesRenderModule>);
