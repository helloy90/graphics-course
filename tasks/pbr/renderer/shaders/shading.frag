#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "terrain/UniformParams.h"
#include "Light.h"
#include "DirectionalLight.h"

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform params {
  UniformParams uniformParams;
};

layout(binding = 1) uniform sampler2D gAlbedo;
layout(binding = 2) uniform sampler2D gNormal;
layout(binding = 3) uniform sampler2D gMaterial;
layout(binding = 4) uniform sampler2D gDepth;

layout(binding = 5) readonly buffer lights {
    Light lightsBuffer[];
};
layout(binding = 6) readonly buffer directionalLights {
    DirectionalLight directionalLightsBuffer[];
};

layout(binding = 7) uniform sampler2D terrainMap;

layout(push_constant) uniform resolution_t {
    uvec2 resolution;
};

void main() {
    vec2 texCoord = gl_FragCoord.xy / resolution;

    vec3 albedo = texture(gAlbedo, texCoord).rgb;
    vec3 normal = texture(gNormal, texCoord).xyz;
    float depth = texture(gDepth, texCoord).x;

    vec4 screenSpacePosition = vec4(texCoord * 2.0 - 1.0, depth, 1.0);

    vec4 viewSpacePosition = inverse(uniformParams.proj) * screenSpacePosition;
    viewSpacePosition /= viewSpacePosition.w;

    vec3 viewSpaceNormal = normalize((transpose(inverse(uniformParams.view)) * vec4(normal, 0)).xyz);

    vec3 ambient = vec3(0.05);
    vec3 color = ambient;

    for (uint i = 0; i < uniformParams.directionalLightsAmount; i++) {
        DirectionalLight currentLight = directionalLightsBuffer[i];

        vec3 viewSpaceLightDirection = normalize(uniformParams.view * vec4(currentLight.direction, 0)).xyz;

        float normalLighting = clamp(dot(viewSpaceNormal, viewSpaceLightDirection), 0.0, 1.0);
        vec3 diffuse = albedo * normalLighting * currentLight.color * currentLight.intensity;

        color += diffuse;
    }

    for (uint i = 0; i < uniformParams.lightsAmount; i++) {
        Light currentLight = lightsBuffer[i];

        vec4 viewSpaceLightPosition = uniformParams.view * currentLight.worldPos;
        viewSpaceLightPosition /= viewSpaceLightPosition.w;

        float dist = length(viewSpaceLightPosition - viewSpacePosition);
        if (dist > currentLight.radius) {
            continue;
        }

        vec3 lightDir = normalize(viewSpaceLightPosition.xyz - viewSpacePosition.xyz);

        float attenuation = 1 / (uniformParams.constant + uniformParams.linear * dist + uniformParams.quadratic * (dist * dist));

        float normalLighting = clamp(dot(viewSpaceNormal, lightDir), 0.0, 1.0);
        vec3 diffuse = albedo * normalLighting * currentLight.color * currentLight.intensity * attenuation;

        color += diffuse;
    }

    fragColor = vec4(color, 1);
}