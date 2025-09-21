#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "UniformParams.h"
#include "Light.h"
#include "DirectionalLight.h"

// for now set in shader
#define SHADOW_CASCADES 3


layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform sampler2D gAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gNormal;
layout(set = 0, binding = 2) uniform sampler2D gMaterial;
layout(set = 0, binding = 3) uniform sampler2D gDepth;
layout(set = 0, binding = 4) uniform sampler2D gShadow[SHADOW_CASCADES];

layout(set = 1, binding = 0) uniform params_t
{
  UniformParams params;
};

layout(set = 1, binding = 1) readonly buffer lights_t
{
  Light lightsBuffer[];
};

layout(set = 1, binding = 2) readonly buffer directional_lights_t
{
  DirectionalLight directionalLightsBuffer[];
};

// fighting alignment rules here
layout(set = 1, binding = 3) readonly buffer shadow_casting_dir_lights_t
{
  vec3 shadowCastingDirLightDirection;
  float shadowCastingDirLightIntensity;
  vec3 shadowCastingDirLightColor;
  uint cascadesAmount;
  float planesOffset;
  float _padding[7];
  mat4 lightProjViews[SHADOW_CASCADES];
  float planes[SHADOW_CASCADES + 1];
};


layout(set = 1, binding = 4) readonly uniform light_params_t
{
  uint lightsAmount;
  uint directionalLightsAmount;
  uint shadowCastingDirLightsAmount; // 1 for now
  float[] _;
};

layout(set = 1, binding = 5) uniform samplerCube cubemap;

layout(push_constant) uniform resolution_t
{
  uvec2 resolution;
};


// PBR -------------------------------------------
const float PI = 3.14159265359;

float clampedDot(vec3 x, vec3 y)
{
  return clamp(dot(x, y), 0.0, 1.0);
}

float heaviside(float x)
{
  if (x > 0.0)
  {
    return 1.0;
  }
  return 0.0;
}

float getAttenuation(float range, float pointDistance)
{
  float distanceSq = pointDistance * pointDistance;
  if (range <= 0)
  {
    return 1.0 / (distanceSq);
  }
  return max(min(1.0 - pow(pointDistance / range, 4.0), 1.0), 0.0) / (distanceSq);
}

vec3 getLightIntensity(Light light, vec3 pointToLight)
{
  float attenuation = getAttenuation(light.radius, length(pointToLight));

  return attenuation * light.intensity * light.color;
}

vec3 getLightIntensity(DirectionalLight light, vec3 pointToLight)
{
  float attenuation = 1.0f;

  return attenuation * light.intensity * light.color;
}

vec3 getIBLGGXFrensel(vec3 normal, vec3 viewDir, float roughness, vec3 f0, float specularWeight)
{
  return vec3(0);
}

vec3 IBL()
{
  return vec3(0);
}

float D_GGX(float alphaRoughness, float NdotH)
{
  float alphaRoughnessSq = alphaRoughness * alphaRoughness;
  float denom = NdotH * NdotH * (alphaRoughnessSq - 1.0) + 1.0;
  return alphaRoughnessSq / (PI * denom * denom);
}

float visibility_GGX_Correlated(float alphaRoughness, float NdotL, float NdotV)
{
  float alphaRoughnessSq = alphaRoughness * alphaRoughness;
  float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
  float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
  float GGX = GGXL + GGXV;
  if (GGX > 0.0)
  {
    return 0.5 / GGX;
  }
  return 0.0;
}

vec3 BRDFSpecular_GGX(float alphaRoughness, float NdotL, float NdotV, float NdotH)
{
  float visibility = visibility_GGX_Correlated(alphaRoughness, NdotL, NdotV);
  float distribution = D_GGX(alphaRoughness, NdotH);

  return vec3(visibility * distribution);
}

vec3 frenselSchlick(vec3 f0, float theta)
{
  return f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - abs(theta), 0.0, 1.0), 5.0);
}

vec3 diffuseBrdf(vec3 color)
{
  return color / PI;
}

// for now is in world space
vec3 computeLightPBR(
  vec3 baseColor, vec3 pos, Light light, vec3 normal, vec3 reflection, vec4 material)
{
  const float roughness = material.g;
  const float metallic = material.b;

  const float alphaRoughness = roughness * roughness;

  const vec3 pointToLight = light.pos - pos;

  const vec3 fromPosToCamera = normalize(params.cameraWorldPosition - pos); // V
  const vec3 fromPosToLight = normalize(pointToLight);                      // L
  const vec3 surfaceNormal = normalize(normal);                             // N
  const vec3 halfVector = normalize(fromPosToLight + fromPosToCamera);      // H

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

  vec3 specularMetal =
    lightIntensity * NdotL * BRDFSpecular_GGX(alphaRoughness, NdotL, NdotV, NdotH);
  vec3 specularDielectric = specularMetal;

  vec3 metalBrdf = metalFrensel * specularMetal;
  vec3 dielectricBrdf = mix(diffuse, specularDielectric, dielectricFrensel);

  return mix(dielectricBrdf, metalBrdf, metallic);
}

// for now is in world space
vec3 computeLightPBR(
  vec3 baseColor, vec3 pos, DirectionalLight light, vec3 normal, vec3 reflection, vec4 material)
{
  const float roughness = material.g;
  const float metallic = material.b;

  const float alphaRoughness = roughness * roughness;

  const vec3 pointToLight = -light.direction;

  const vec3 fromPosToCamera = normalize(params.cameraWorldPosition - pos); // V
  const vec3 fromPosToLight = normalize(pointToLight);                      // L
  const vec3 surfaceNormal = normalize(normal);                             // N
  const vec3 halfVector = normalize(fromPosToLight + fromPosToCamera);      // H

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

  vec3 specularMetal =
    lightIntensity * NdotL * BRDFSpecular_GGX(alphaRoughness, NdotL, NdotV, NdotH);
  vec3 specularDielectric = specularMetal;

  vec3 metalBrdf = metalFrensel * specularMetal;
  vec3 dielectricBrdf = mix(diffuse, specularDielectric, dielectricFrensel);

  return mix(dielectricBrdf, metalBrdf, metallic);
}
// -----------------------------------------------

uint getShadowCascade(float depth)
{
  uint cascade = 0;
  for (; cascade < cascadesAmount; cascade++)
  {
    if (depth < planes[cascade + 1])
    {
      break;
    }
  }

  return cascade;
}

float getShadowFromTexture(
  vec2 shadowTexCoord, vec2 offset, float depth, float bias, uint currentCascade)
{
  const vec2 currentTexCoord = shadowTexCoord + offset;
  const float lightDepth = texture(gShadow[currentCascade], currentTexCoord).x;
  float shadow = (depth < lightDepth + bias) ? 0.0 : 1.0;

  return shadow;
}

float computeShadow(vec2 shadowTexCoord, float depth, float bias, uint currentCascade)
{
  if (depth > 1.0)
  {
    return 0.0;
  }

  float shadow = 0.0;
  vec2 texelSize = 1.0 / vec2(textureSize(gShadow[0], 0));

  int range = 1;
  int count = 0;

  for (int x = -range; x <= range; x++)
  {
    for (int y = -range; y <= range; y++)
    {
      shadow +=
        getShadowFromTexture(shadowTexCoord, vec2(x, y) * texelSize, depth, bias, currentCascade);
      count++;
    }
  }

  shadow /= float(count);

  return shadow;
}

void main()
{
  const vec2 texCoord = gl_FragCoord.xy / resolution;

  const vec3 albedo = texture(gAlbedo, texCoord).rgb;

  const vec3 normal = normalize(texture(gNormal, texCoord).xyz);
  const vec3 viewSpaceNormal = normalize((transpose(params.invView) * vec4(normal, 0.0)).xyz);

  const vec4 material = texture(gMaterial, texCoord);
  const float depth = texture(gDepth, texCoord).x;

  const vec4 screenSpacePosition = vec4(texCoord * 2.0 - 1.0, depth, 1.0);

  vec4 viewSpacePosition = params.invProj * screenSpacePosition;
  viewSpacePosition /= viewSpacePosition.w;

  vec4 worldSpacePosition = (params.invProjView * screenSpacePosition);
  worldSpacePosition /= worldSpacePosition.w;

  const vec3 viewDirection = (worldSpacePosition.xyz - params.cameraWorldPosition);
  const vec3 reflection = texture(cubemap, reflect(viewDirection, normal)).rgb;

  // change to IBL later
  vec3 color = vec3(albedo * 0.3);

  vec3 skyboxTexCoord = (params.invProjViewMat3 * screenSpacePosition).xyz;
  vec3 skyboxColor = texture(cubemap, normalize(skyboxTexCoord)).rgb;

  DirectionalLight shadowCastingDirLight = {
    shadowCastingDirLightDirection, shadowCastingDirLightIntensity, shadowCastingDirLightColor};

  vec3 point = shadowCastingDirLight.color *
    pow(clampedDot(normalize(-viewDirection), normalize(shadowCastingDirLight.direction)), 3500.0);

  uint currentCascade = getShadowCascade(viewSpacePosition.z);

  vec3 shadowColor = vec3(0, 0, 0);
  if (params.colorShadows)
  {
    switch (currentCascade)
    {
    case 0:
      shadowColor.rgb = vec3(1.0f, 0.25f, 0.25f);
      break;
    case 1:
      shadowColor.rgb = vec3(0.25f, 1.0f, 0.25f);
      break;
    case 2:
      shadowColor.rgb = vec3(0.25f, 0.25f, 1.0f);
      break;
    case 3:
      shadowColor.rgb = vec3(1.0f, 0.25f, 1.0f);
      break;
    }
  }

  float shadowBias = 0.005 * tan(acos(clampedDot(normal, shadowCastingDirLight.direction)));
  shadowBias = clamp(shadowBias, 0.001, 0.01);

  const vec4 lightSpacePos = (lightProjViews[currentCascade]) * worldSpacePosition;
  vec3 lightSpaceNDCPos = lightSpacePos.xyz / lightSpacePos.w;

  const vec2 shadowTexCoord = lightSpaceNDCPos.xy * 0.5 + vec2(0.5);

  const float shadow = (params.usePCF)
    ? computeShadow(shadowTexCoord, lightSpaceNDCPos.z, shadowBias, currentCascade)
    : getShadowFromTexture(
        shadowTexCoord, vec2(0, 0), lightSpaceNDCPos.z, shadowBias, currentCascade);

  vec3 pbrColor = computeLightPBR(
    albedo, worldSpacePosition.xyz, shadowCastingDirLight, normal, reflection, material);
  skyboxColor += point;
  color += pbrColor * (1.0 - shadow) + shadowColor * shadow;

  for (uint i = 0; i < directionalLightsAmount; i++)
  {
    DirectionalLight currentLight = directionalLightsBuffer[i];

    // sun
    vec3 point = currentLight.color *
      pow(clampedDot(normalize(-viewDirection), normalize(currentLight.direction)), 3500.0);

    vec3 pbrColor =
      computeLightPBR(albedo, worldSpacePosition.xyz, currentLight, normal, reflection, material);
    skyboxColor += point;
    color += pbrColor;
  }

  for (uint i = 0; i < lightsAmount; i++)
  {
    Light currentLight = lightsBuffer[i];

    float dist = length(currentLight.pos - worldSpacePosition.xyz);
    if (dist > currentLight.radius)
    {
      continue;
    }

    vec3 pbrColor =
      computeLightPBR(albedo, worldSpacePosition.xyz, currentLight, normal, reflection, material);
    color += pbrColor;
  }

  fragColor = vec4(depth >= 1.0 ? skyboxColor : color, 1);
}
