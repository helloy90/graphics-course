#pragma once

#include <glm/glm.hpp>

// including info for velocity buffer constuction
struct RenderPacket
{
  glm::mat4x4 projView;
  glm::mat4x4 previousProjView;
  glm::vec2 currentJitter;
  glm::vec2 previousJitter;
  glm::vec3 cameraWorldPosition;
  float time;
  glm::uvec2 resolution;
};
