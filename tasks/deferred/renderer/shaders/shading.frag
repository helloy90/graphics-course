#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "terrain/UniformParams.h"

layout(location = 0) out vec4 fragColor;

layout (binding = 0) uniform params {
  UniformParams uniformParams;
};

layout(binding = 1) uniform sampler2D gAlbedo;
layout(binding = 2) uniform sampler2D gNormal;
layout(binding = 3) uniform sampler2D gDepth;

layout(push_constant) uniform resolution_t {
    uvec2 resolution;
};

struct Light {
    vec3 pos;
    vec3 color;
    float intensity;
};

void main() {
    vec2 texCoord = gl_FragCoord.xy / resolution;

    vec3 albedo = texture(gAlbedo, texCoord).rgb;
    vec3 normal = texture(gNormal, texCoord).xyz;
    float depth = texture(gDepth, texCoord).x;

    vec4 screenSpacePosition = vec4(texCoord * 2.0 - 1.0, depth, 1.0);

    vec4 viewSpacePosition = inverse(uniformParams.proj) * screenSpacePosition;
    viewSpacePosition /= viewSpacePosition.w;

    Light light = {(uniformParams.view * vec4(10, 20, 10, 1.0)).xyz, vec3(1), 1.0};

    vec3 ambient = vec3(0.1);

    vec3 lightDir = normalize(light.pos - viewSpacePosition.xyz);

    float normalLighting = clamp(dot(lightDir, normal), 0.0, 1.0);
    vec3 diffuse = albedo * normalLighting * light.color * light.intensity;

    vec3 color = ambient + diffuse;

    fragColor = vec4(color, 1);
}