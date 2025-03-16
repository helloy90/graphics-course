#version 460
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

layout(binding = 7) uniform samplerCube cubemap;

layout(push_constant) uniform resolution_t {
    uvec2 resolution;
};


// PBR -------------------------------------------
const float PI = 3.14159265359;

float clampedDot(vec3 x, vec3 y) {
    return clamp(dot(x, y), 0.0, 1.0);
}

float heaviside(float x) {
    if (x > 0.0) {
        return 1.0;
    }
    return 0.0;
}

float getAttenuation(float range, float pointDistance) {
    float distanceSq = pointDistance * pointDistance;
    if (range <= 0) {
        return 1.0 / (distanceSq);
    }
    return max(min(1.0 - pow(pointDistance / range, 4.0), 1.0), 0.0) / (distanceSq);
}

vec3 getLightIntensity(Light light, vec3 pointToLight) {
    float attenuation = getAttenuation(light.radius, length(pointToLight));

    return attenuation * light.intensity * light.color;
}

vec3 getLightIntensity(DirectionalLight light, vec3 pointToLight) {
    float attenuation = 1.0f;

    return attenuation * light.intensity * light.color;
}

float D_GGX(float alphaRoughness, float NdotH) {
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float denom = NdotH * NdotH * (alphaRoughnessSq - 1.0) + 1.0;
    return alphaRoughnessSq / (PI * denom * denom);
}

float visibility_GGX_Correlated(float alphaRoughness, float NdotL, float NdotV) {
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGX = GGXL + GGXV;
    if (GGX > 0.0) {
        return 0.5 / GGX;
    }
    return 0.0;
}

vec3 BRDFSpecular_GGX(float alphaRoughness, float NdotL, float NdotV, float NdotH) {
    float visibility = visibility_GGX_Correlated(alphaRoughness, NdotL, NdotV);
    float distribution = D_GGX(alphaRoughness, NdotH);

    return vec3(visibility * distribution);
}

vec3 frenselSchlick(vec3 f0, float theta) {
    return f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - abs(theta), 0.0, 1.0), 5.0);
}

vec3 diffuseBrdf(vec3 color) {
    return color / PI;
}

// for now is in world space
vec3 computeLightPBR(vec3 baseColor, vec3 pos, Light light, vec3 normal, vec3 reflection, vec4 material) {
    const float roughness = material.g;
    const float metallic = material.b;

    const float alphaRoughness = roughness * roughness;

    const vec3 pointToLight = light.worldPos.xyz - pos;

    const vec3 fromPosToCamera = normalize(uniformParams.cameraWorldPosition - pos); // V
    const vec3 fromPosToLight = normalize(pointToLight); // L
    const vec3 surfaceNormal = normalize(normal); // N
    const vec3 halfVector = normalize(fromPosToLight + fromPosToCamera); // H

    const float VdotH = clampedDot(fromPosToCamera, halfVector);
    const float HdotL = clampedDot(halfVector, fromPosToLight);
    const float NdotL = clampedDot(surfaceNormal, fromPosToLight);
    const float NdotV = clampedDot(surfaceNormal, fromPosToCamera) + 0.00001;
    const float NdotH = clampedDot(surfaceNormal, halfVector);

    vec3 lightIntensity = getLightIntensity(light, pointToLight);

    vec3 f0 = vec3(0.04);
    vec3 dielectricFrensel = frenselSchlick(f0, VdotH);
    vec3 metalFrensel = frenselSchlick(baseColor * reflection, VdotH);
    vec3 diffuse = lightIntensity * NdotL * diffuseBrdf(baseColor);

    vec3 specularMetal = lightIntensity * NdotL * BRDFSpecular_GGX(alphaRoughness, NdotL, NdotV, NdotH);
    vec3 specularDielectric = specularMetal;

    vec3 metalBrdf = metalFrensel * specularMetal;
    vec3 dielectricBrdf = mix(diffuse, specularDielectric, dielectricFrensel);

    return mix(dielectricBrdf, metalBrdf, metallic);
}

// for now is in world space
vec3 computeLightPBR(vec3 baseColor, vec3 pos, DirectionalLight light, vec3 normal, vec3 reflection, vec4 material) {
    const float roughness = material.g;
    const float metallic = material.b;

    const float alphaRoughness = roughness * roughness;

    const vec3 pointToLight = -light.direction;

    const vec3 fromPosToCamera = normalize(uniformParams.cameraWorldPosition - pos); // V
    const vec3 fromPosToLight = normalize(pointToLight); // L
    const vec3 surfaceNormal = normalize(normal); // N
    const vec3 halfVector = normalize(fromPosToLight + fromPosToCamera); // H

    const float VdotH = clampedDot(fromPosToCamera, halfVector);
    const float HdotL = clampedDot(halfVector, fromPosToLight);
    const float NdotL = clampedDot(surfaceNormal, fromPosToLight);
    const float NdotV = clampedDot(surfaceNormal, fromPosToCamera) + 0.00001;
    const float NdotH = clampedDot(surfaceNormal, halfVector);

    vec3 lightIntensity = getLightIntensity(light, pointToLight);

    vec3 f0 = vec3(0.04);
    vec3 dielectricFrensel = frenselSchlick(f0, VdotH);
    vec3 metalFrensel = frenselSchlick(baseColor * reflection, VdotH);
    vec3 diffuse = lightIntensity * NdotL * diffuseBrdf(baseColor);

    vec3 specularMetal = lightIntensity * NdotL * BRDFSpecular_GGX(alphaRoughness, NdotL, NdotV, NdotH);
    vec3 specularDielectric = specularMetal;

    vec3 metalBrdf = metalFrensel * specularMetal;
    vec3 dielectricBrdf = mix(diffuse, specularDielectric, dielectricFrensel);

    return mix(dielectricBrdf, metalBrdf, metallic);
}
// -----------------------------------------------

void main() {
    const vec2 texCoord = gl_FragCoord.xy / resolution;

    const vec3 albedo = texture(gAlbedo, texCoord).rgb;
    const vec3 normal = texture(gNormal, texCoord).xyz;
    const vec4 material = texture(gMaterial, texCoord);
    const float depth = texture(gDepth, texCoord).x;

    const vec4 screenSpacePosition = vec4(texCoord * 2.0 - 1.0, depth, 1.0);

    vec4 viewSpacePosition = uniformParams.invProj * screenSpacePosition;
    viewSpacePosition /= viewSpacePosition.w;

    const vec3 viewSpaceNormal = normalize((transpose(uniformParams.invView) * vec4(normal, 0.0)).xyz);
    
    vec4 worldSpacePosition = (uniformParams.invProjView * screenSpacePosition);
    worldSpacePosition /= worldSpacePosition.w;

    const vec3 reflection = texture(cubemap, reflect((worldSpacePosition.xyz - uniformParams.cameraWorldPosition), normal)).rgb * 20;

    // change to IBL later
    vec3 color = vec3(0);

    vec3 skyboxTexCoord = (uniformParams.invProjViewMat3 * screenSpacePosition).xyz;
    vec3 skyboxColor = texture(cubemap, normalize(skyboxTexCoord)).rgb;

    for (uint i = 0; i < uniformParams.directionalLightsAmount; i++) {
        DirectionalLight currentLight = directionalLightsBuffer[i];

        vec3 pbrColor = computeLightPBR(albedo, worldSpacePosition.xyz, currentLight, normal, reflection, material);
        color += pbrColor;
    }

    for (uint i = 0; i < uniformParams.lightsAmount; i++) {
        Light currentLight = lightsBuffer[i];

        float dist = length(currentLight.worldPos.xyz - worldSpacePosition.xyz);
        if (dist > currentLight.radius) {
            continue;
        }

        vec3 pbrColor = computeLightPBR(albedo, worldSpacePosition.xyz, currentLight, normal, reflection, material);
        color += pbrColor;
    }

    fragColor = vec4(depth >= 1.0 ? skyboxColor : color, 1);
}