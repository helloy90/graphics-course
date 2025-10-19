#include "SSAOModule.hpp"

#include <random>

#include <tracy/Tracy.hpp>

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>

#include "render_utils/Utilities.hpp"


SSAOModule::SSAOModule() {}

void SSAOModule::allocateResources(const AllocationInfo& info)
{
  auto& ctx = etna::get_context();

  std::mt19937 generator(info.seed);
  std::uniform_real_distribution<float> firstRange(-1.0f, 1.0f);
  std::uniform_real_distribution<float> secondRange(0.0f, 1.0f);

  uint32_t noiseTextureTexelsAmount = info.noiseTextureSize.x * info.noiseTextureSize.y;
  std::vector<glm::vec2> noise;
  noise.reserve(noiseTextureTexelsAmount);

  for (uint32_t i = 0; i < noiseTextureTexelsAmount; i++)
  {
    noise.emplace_back(firstRange(generator), firstRange(generator));
  }

  kernelSize = info.kernelSize;
  std::vector<glm::vec3> kernel;
  kernel.reserve(kernelSize);

  for (uint32_t i = 0; i < kernelSize; i++)
  {
    glm::vec3 val = {firstRange(generator), firstRange(generator), secondRange(generator)};

    float scale = static_cast<float>(i) / kernelSize;
    float scaleMult = std::lerp(0.1f, 1.0f, scale * scale);

    kernel.emplace_back(val * scaleMult);
  }

  noiseTexture = ctx.createImage(
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{info.noiseTextureSize.x, info.noiseTextureSize.y, 1},
      .name = "ssaoNoiseTexture",
      .format = vk::Format::eR32G32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
    });

  ssaoKernel = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = kernelSize * sizeof(glm::vec3),
      .bufferUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = fmt::format("ssaoKernelBuffer")});

  noiseSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .filter = vk::Filter::eNearest,
      .addressMode = vk::SamplerAddressMode::eRepeat,
      .name = "ssaoNoiseSampler",
    });

  oneShotCommands = ctx.createOneShotCmdMgr();

  transferHelper = std::make_unique<etna::BlockingTransferHelper>(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 4});


  etna::Buffer tempBuffer = ctx.createBuffer(
    etna::Buffer::CreateInfo{
      .size = noiseTextureTexelsAmount * sizeof(glm::vec2),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer |
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc,
      .memoryUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
      .name = fmt::format("tempNoiseBufferSSAO")});


  transferHelper->uploadBuffer(*oneShotCommands, tempBuffer, 0, std::as_bytes(std::span(noise)));

  render_utility::local_copy_buffer_to_image(*oneShotCommands, tempBuffer, noiseTexture, 1);

  tempBuffer.reset();

  transferHelper->uploadBuffer(*oneShotCommands, ssaoKernel, 0, std::as_bytes(std::span(kernel)));

  auto cmdBuf = oneShotCommands->start();
  ETNA_CHECK_VK_RESULT(cmdBuf.begin(vk::CommandBufferBeginInfo{}));
  {
    etna::set_state(
      cmdBuf,
      noiseTexture.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(cmdBuf);
  }
  ETNA_CHECK_VK_RESULT(cmdBuf.end());

  oneShotCommands->submitAndWait(cmdBuf);
}

void SSAOModule::loadShaders()
{
  etna::create_program("ssao", {AMBIENT_OCCLUSION_MODULE_SHADERS_ROOT "ssao.comp.spv"});
}

void SSAOModule::setupPipelines()
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  ssaoPipeline = pipelineManager.createComputePipeline("ssao", {});
}

void SSAOModule::execute(
  vk::CommandBuffer cmd_buf,
  const etna::Image& normal_texture,
  const etna::Image& depth_texture,
  const etna::Sampler& depth_sampler,
  const etna::Image& occlusion_texture)
{
  ZoneScoped;

  auto extent = occlusion_texture.getExtent();

  {
    ETNA_PROFILE_GPU(cmd_buf, ssao);

    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, ssaoPipeline.getVkPipeline());

    auto shaderInfo = etna::get_shader_program("ssao");

    auto set = etna::create_descriptor_set(
      shaderInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{0, normal_texture.genBinding({}, vk::ImageLayout::eGeneral)},
       etna::Binding{
         1, depth_texture.genBinding(depth_sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
       etna::Binding{2, occlusion_texture.genBinding({}, vk::ImageLayout::eGeneral)},
       etna::Binding{3, noiseTexture.genBinding(noiseSampler.get(), vk::ImageLayout::eGeneral)},
       etna::Binding{4, ssaoKernel.genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute, ssaoPipeline.getVkPipelineLayout(), 0, {set.getVkSet()}, {});

    cmd_buf.pushConstants<uint32_t>(
      ssaoPipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, kernelSize);

    cmd_buf.dispatch(
      (static_cast<uint32_t>(extent.width) + 31) / 32,
      (static_cast<uint32_t>(extent.height) + 31) / 32,
      1);
  }
}
