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

float trowbridge_reitz_d(float squaredRoughness, float NdotH) {
    float fraction = squaredRoughness / (NdotH * NdotH * (squaredRoughness * squaredRoughness - 1.0) + 1.0);
    return fraction * fraction / PI;
}

float visibility_term(float squaredRoughness, float product) {
    return abs(product) + sqrt(squaredRoughness * squaredRoughness + (1 - squaredRoughness * squaredRoughness) * (product * product));
}

float visibility(float squaredRoughness, float HdotL, float NdotL, float HdotV, float NdotV) {
    return 1 / (visibility_term(squaredRoughness, NdotL) * visibility_term(squaredRoughness, NdotV));
}

vec3 conductor_frensel(vec3 baseColor, vec3 baseColorGrazingReflection, vec3 bsdf, float VdotH_term) {
    return bsdf * (baseColor + (baseColorGrazingReflection - baseColor) * VdotH_term);
}

vec3 specular_brdf(float squaredRoughness, float NdotH, float HdotL, float NdotL, float HdotV, float NdotV) {
    float vis = visibility(squaredRoughness, HdotL, NdotL, HdotV, NdotV);
    float ggx_D = trowbridge_reitz_d(squaredRoughness, NdotH);
    return vec3(vis * ggx_D);
}

vec3 diffuse_brdf(vec3 color) {
    return color / PI;
}

vec3 frensel_mix(float f0, vec3 base, vec3 layer, float VdotH_term) {
    float frensel = f0 + (1.0 - f0) * VdotH_term;
    return mix(base, layer, frensel);
}
// all vectors should be in one coordinate system
vec3 computeLightPBR(vec3 baseColor, vec3 pos, vec3 lightDir, vec3 normal, vec3 reflection, vec4 material) {
    const float roughness = material.g;
    const float metallic = material.b;

    const float squaredRoughness = roughness;// * roughness;

    const vec3 fromPosToCamera = normalize(-pos); // V
    const vec3 fromPosToLight = normalize(lightDir - pos); // L
    const vec3 surfaceNormal = normalize(normal); // N
    const vec3 halfVector = normalize(fromPosToLight + fromPosToCamera); // H

    const float VdotH = clampedDot(inverse(mat3(uniformParams.view)) * fromPosToCamera, inverse(mat3(uniformParams.view)) * halfVector);
    const float HdotL = clampedDot(halfVector, fromPosToLight);
    const float NdotL = clampedDot(surfaceNormal, fromPosToLight);
    const float HdotV = clampedDot(halfVector, fromPosToCamera);
    const float NdotV = clampedDot(surfaceNormal, fromPosToCamera);
    const float NdotH = clampedDot(surfaceNormal, halfVector);

    const float VdotH_term = pow(clamp(1.0 - abs(VdotH), 0.0, 1.0), 5.0);

    const vec3 specular_brdf_value = 
        specular_brdf(
            squaredRoughness,
            NdotH,
            HdotL, 
            NdotL, 
            HdotV, 
            NdotV
        );

    const vec3 metal_brdf = 
        conductor_frensel(
            baseColor * reflection, 
            vec3(1.0), // 90 angle reflection almost white
            specular_brdf_value,
            VdotH_term
        );

    // if indexOfReflection = 1.5 then f0 = 0.04
    const vec3 dielectric_brdf = 
        frensel_mix(
            0.04, // f0 precompute
            diffuse_brdf(baseColor), 
            specular_brdf_value, 
            VdotH_term
        );

    return mix(dielectric_brdf, metal_brdf, metallic);
}
// -----------------------------------------------

void main() {
    const vec2 texCoord = gl_FragCoord.xy / resolution;

    const vec3 albedo = texture(gAlbedo, texCoord).rgb;
    const vec3 normal = texture(gNormal, texCoord).xyz;
    const vec4 material = texture(gMaterial, texCoord);
    const float depth = texture(gDepth, texCoord).x;

    const vec4 screenSpacePosition = vec4(texCoord * 2.0 - 1.0, depth, 1.0);

    vec4 viewSpacePosition = inverse(uniformParams.proj) * screenSpacePosition;
    viewSpacePosition /= viewSpacePosition.w;

    const vec3 viewSpaceNormal = normalize((transpose(inverse(uniformParams.view)) * vec4(normal, 0.0)).xyz);
    
    const vec3 worldSpacePosition = (inverse(uniformParams.projView) * screenSpacePosition).xyz;

    const vec3 reflection = texture(cubemap, reflect(worldSpacePosition, normal)).rgb;

    vec3 color = vec3(0);

    for (uint i = 0; i < uniformParams.directionalLightsAmount; i++) {
        DirectionalLight currentLight = directionalLightsBuffer[i];

        vec3 viewSpaceLightDirection = normalize(uniformParams.view * vec4(currentLight.direction, 0.0)).xyz;

        color += 
            computeLightPBR(albedo, viewSpacePosition.xyz, viewSpaceLightDirection, viewSpaceNormal, reflection, material) 
            * currentLight.color 
            * currentLight.intensity;
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

        color += 
            (dot(viewSpaceNormal, -lightDir) < 0 ? vec3(0) : computeLightPBR(albedo, viewSpacePosition.xyz, lightDir, viewSpaceNormal, reflection, material)) 
            * currentLight.color
            * currentLight.intensity
            * attenuation;
    }

    fragColor = vec4(color, 1);
}