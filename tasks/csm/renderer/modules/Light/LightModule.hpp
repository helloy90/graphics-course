#pragma once

#include <etna/ComputePipeline.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>
#include <etna/BlockingTransferHelper.hpp>

#include "DirectionalLight.h"
#include "Light.h"
#include "shaders/LightParams.h"
#include "HeightParams.hpp"


class LightModule
{
public:
  LightModule();

  void allocateResources();
  void loadShaders();
  void setupPipelines();

  void loadLights();
  void displaceLights(
    HeightParams height_params,
    const etna::Image& terrain_map,
    const etna::Image& terrain_normal_map,
    const etna::Sampler& terrain_sampler);

  void drawGui(
    HeightParams height_params,
    const etna::Image& terrain_map,
    const etna::Image& terrain_normal_map,
    const etna::Sampler& terrain_sampler);

  const etna::Buffer& getLightParamsBuffer() const { return paramsBuffer; }
  const etna::Buffer& getPointLightsBuffer() const { return lightsBuffer; }
  const etna::Buffer& getDirectionalLightsBuffer() const { return directionalLightsBuffer; }

private:
  LightParams params;
  etna::Buffer paramsBuffer;

  std::vector<Light> lights;
  std::vector<DirectionalLight> directionalLights;

  etna::Buffer lightsBuffer;
  etna::Buffer directionalLightsBuffer;

  etna::ComputePipeline lightDisplacementPipeline;

  std::unique_ptr<etna::OneShotCmdMgr> oneShotCommands;
  std::unique_ptr<etna::BlockingTransferHelper> transferHelper;
};
