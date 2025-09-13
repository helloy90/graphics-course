#pragma once

#include "DirectionalLight.h"
#include "scene/Camera.hpp"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/fwd.hpp>


class ShadowCastingDirectionalLight
{
public:
  struct CreateInfo
  {
    DirectionalLight light;
    glm::vec3 position;
    float radius;
    float distance;
  };

  struct ShaderInfo
  {
    glm::mat4x4 proj;
    DirectionalLight light;
  };

public:
  ShadowCastingDirectionalLight() = default;

  explicit ShadowCastingDirectionalLight(const CreateInfo& info)
  {
    shadowCamera.lookAt(info.position, {0, 0, 0}, {0, 1, 0});

    shaderInfo = {
      .proj = glm::orthoLH_ZO(
                info.radius, -info.radius, info.radius, -info.radius, 0.0f, info.distance) *
        shadowCamera.viewTm(),
      .light = info.light};
  }

  const ShaderInfo& getInfo() const { return shaderInfo; }

private:
  ShaderInfo shaderInfo;
  Camera shadowCamera;
};
