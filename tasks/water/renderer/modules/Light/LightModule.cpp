#include "LightModule.hpp"

#include <imgui.h>

#include <etna/PipelineManager.hpp>


LightModule::LightModule()
  : params(
      {.lightsAmount = 0,
       .directionalLightsAmount = 0,
       .constant = 1.0f,
       .linear = 0.14f,
       .quadratic = 0.07f})
{
}

void LightModule::allocateResources()
{
  paramsBuffer = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(params),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_AUTO,
    .allocationCreate =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
    .name = "meshesParams"});

  oneShotCommands = etna::get_context().createOneShotCmdMgr();

  transferHelper = std::make_unique<etna::BlockingTransferHelper>(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 6});

  loadLights();
}

void LightModule::loadShaders()
{
  etna::create_program(
    "lights_displacement", {LIGHTS_MODULE_SHADERS_ROOT "displace_lights.comp.spv"});
}

void LightModule::setupPipelines()
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  lightDisplacementPipeline = pipelineManager.createComputePipeline("lights_displacement", {});
}

void LightModule::loadLights()
{
  auto& ctx = etna::get_context();

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
    float lightMax = std::max({light.color.r, light.color.g, light.color.b});
    light.radius = (-params.linear +
                    static_cast<float>(glm::sqrt(
                      params.linear * params.linear -
                      4 * params.quadratic * (params.constant - (256.0 / 5.0) * lightMax)))) /
      (2 * params.quadratic);
    // spdlog::info("radius - {}", light.radius);
  }

  directionalLights = {DirectionalLight{
    .direction = glm::vec3{1, -0.35, -3},
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

  paramsBuffer.map();
  std::memcpy(paramsBuffer.data(), &params, sizeof(LightParams));
  paramsBuffer.unmap();
}

void LightModule::displaceLights(
  const etna::Buffer& height_params_buffer,
  const etna::Image& terrain_map,
  const etna::Image& terrain_normal_map,
  const etna::Sampler& terrain_sampler)
{
  auto commandBuffer = oneShotCommands->start();

  ETNA_CHECK_VK_RESULT(commandBuffer.begin(vk::CommandBufferBeginInfo{}));
  {
    etna::set_state(
      commandBuffer,
      terrain_map.get(),
      vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderStorageRead,
      vk::ImageLayout::eGeneral,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      commandBuffer,
      terrain_normal_map.get(),
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
        {etna::Binding{0, paramsBuffer.genBinding()},
         etna::Binding{1, height_params_buffer.genBinding()},
         etna::Binding{2, terrain_map.genBinding(terrain_sampler.get(), vk::ImageLayout::eGeneral)},
         etna::Binding{3, lightsBuffer.genBinding()}});

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

      commandBuffer.dispatch((lights.size() + 127) / 128, 1, 1);
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
      terrain_map.get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::set_state(
      commandBuffer,
      terrain_normal_map.get(),
      vk::PipelineStageFlagBits2::eTessellationEvaluationShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);

    etna::flush_barriers(commandBuffer);
  }
  ETNA_CHECK_VK_RESULT(commandBuffer.end());

  oneShotCommands->submitAndWait(commandBuffer);
}

// TODO - find more elegant way
void LightModule::drawGui(
  const etna::Buffer& height_params_buffer,
  const etna::Image& terrain_map,
  const etna::Image& terrain_normal_map,
  const etna::Sampler& terrain_sampler)
{
  ImGui::Begin("Application Settings");

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
      ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
      transferHelper->uploadBuffer(
        *oneShotCommands, directionalLightsBuffer, 0, std::as_bytes(std::span(directionalLights)));
      directionalLightsChanged = false;
    }
    if (lightsChanged)
    {
      ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
      transferHelper->uploadBuffer(
        *oneShotCommands, lightsBuffer, 0, std::as_bytes(std::span(lights)));
      displaceLights(height_params_buffer, terrain_map, terrain_normal_map, terrain_sampler);
      lightsChanged = false;
    }
  }

  ImGui::End();
}
