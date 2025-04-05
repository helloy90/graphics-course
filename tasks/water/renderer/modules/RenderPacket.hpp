#pragma once

#include <glm/glm.hpp>

struct RenderPacket {
    glm::mat4x4 projView;
    glm::vec3 cameraWorldPosition;
    uint32_t _padding0 = 0;
};