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
    vec3 texCoord = (mat3(uniformParams.invProjView) * screenSpacePosition);
    fragColor = vec4(texture(cubemap, normalize(texCoord.xyz)).rgb, 1.0);
}