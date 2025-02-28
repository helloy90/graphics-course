#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "terrain/UniformParams.h"

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform params {
  UniformParams uniformParams;
};

layout(binding = 1) uniform samplerCube cubemap;

layout(push_constant) uniform resolution_t {
    uvec2 resolution;
};

void main() {
    const vec3 screenSpacePosition = vec3(gl_FragCoord.xy / resolution * 2.0 - 1.0, 1.0);
    vec3 texCoord = normalize(inverse(mat3(uniformParams.projView)) * screenSpacePosition);
    fragColor = vec4(texture(cubemap, texCoord).rgb, 1.0);
}