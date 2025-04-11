#pragma once

#include <glm/glm.hpp>

struct RenderPacket {
    glm::mat4x4 projView;
    glm::vec3 cameraWorldPosition;
    float time;
};