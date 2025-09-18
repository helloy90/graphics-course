#pragma once

#include <etna/ComputePipeline.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>
#include <etna/BlockingTransferHelper.hpp>

#include "DirectionalLight.h"
#include "Light.h"
#include "ShadowCastingDirectionalLight.hpp"
#include "etna/DescriptorSet.hpp"
#include "shaders/LightParams.h"


class LightModule
{
public:
  LightModule();

  void allocateResources();
  void loadShaders();
  void setupPipelines();

  void loadLights(
    const std::vector<Light>& new_lights,
    const std::vector<DirectionalLight>& new_directional_lights,
    ShadowCastingDirectionalLight new_shadow_casting_dir_light);
  void displaceLights();

  void update(const Camera& main_camera, float aspect_ratio);

  void drawGui();

  void loadMaps(const std::vector<etna::Binding>& terrain_bindings);

  const etna::Buffer& getLightParamsBuffer() const { return paramsBuffer; }
  const etna::Buffer& getPointLightsBuffer() const { return lightsBuffer; }
  const etna::Buffer& getDirectionalLightsBuffer() const { return directionalLightsBuffer; }
  const etna::Buffer& getShadowCastingDirLightInfoBuffer() const
  {
    return shadowCastingDirLights.getInfoBuffer();
  }

  etna::Binding getShadowCastingDirLightMatrixBinding(uint32_t index, uint32_t cascade_index) const
  {
    return etna::Binding{
      index,
      getShadowCastingDirLightInfoBuffer().genBinding(
        sizeof(ShadowCastingDirectionalLight::ShaderInfo) +
        // sizeof(ShadowCastingDirectionalLight::ShaderInfo::light) +
        //   sizeof(ShadowCastingDirectionalLight::ShaderInfo::cascadesAmount) +
          // sizeof(ShadowCastingDirectionalLight::ShaderInfo::_padding) +
          sizeof(glm::mat4x4) * cascade_index,
        sizeof(glm::mat4x4))};
  }

private:
  LightParams params;
  etna::Buffer paramsBuffer;

  std::vector<Light> lights;
  std::vector<DirectionalLight> directionalLights;

  // For now only one shadow casting light
  ShadowCastingDirectionalLight shadowCastingDirLights;

  etna::Buffer lightsBuffer;
  etna::Buffer directionalLightsBuffer;

  etna::ComputePipeline lightDisplacementPipeline;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;

  std::unique_ptr<etna::PersistentDescriptorSet> terrainSet;
};
