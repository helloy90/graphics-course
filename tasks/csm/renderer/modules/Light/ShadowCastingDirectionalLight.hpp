#pragma once

#include <vector>

#include <etna/Buffer.hpp>

#include "scene/Camera.hpp"
#include "DirectionalLight.h"


class ShadowCastingDirectionalLight
{
public:
  struct CreateInfo
  {
    DirectionalLight light;
    std::vector<float> planes; // including camera near and far planes
    float planesOffset;
    float shadowMapSize;
  };

  // repack this struct later
  struct ShaderInfo
  {
    DirectionalLight light;
    uint32_t cascadesAmount;
    float planesOffset;
    float _padding[7] = {};
  };

public:
  ShadowCastingDirectionalLight() = default;

  explicit ShadowCastingDirectionalLight(const CreateInfo& info);

  void update(const Camera& main_camera, float aspect_ratio);

  const ShaderInfo& getInfo() const { return shaderInfo; }
  const etna::Buffer& getInfoBuffer() const { return infoBuffer; }

private:
  std::array<glm::vec3, 8> getWorldSpaceFrustumCorners(const glm::mat4x4& proj_view);

  glm::vec3 getFrustumCenter(const std::array<glm::vec3, 8>& corners);

private:
  ShaderInfo shaderInfo;
  std::vector<glm::mat4x4> projViewMatrices;
  std::vector<float> planes;
  float shadowMapSize;
  etna::Buffer infoBuffer;

  Camera shadowCamera;
};
