#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "WaterRenderParams.h"

#include "DirectionalLight.h"


layout(location = 0) in VS_OUT
{
  vec3 pos;
  vec3 normal;
  vec2 texCoord;
};

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform render_params_t
{
  WaterRenderParams params;
};

layout(binding = 2) uniform sampler2D heightMap;
layout(binding = 3) uniform sampler2D normalMap;
layout(binding = 4) uniform sampler2D gShadow;
layout(binding = 5) uniform samplerCube skybox;

layout(binding = 6) readonly buffer shadow_casting_dir_lights_t
{
  mat4 lightProjView;
  DirectionalLight shadowCastingDirLight;
};

layout(push_constant) uniform push_constant_t
{
  mat4 projView;
  vec4 cameraWorldPosition;
};


const float kPi = 3.1415926535897932384626433832795;

float clampedDot(vec3 x, vec3 y)
{
  return clamp(dot(x, y), 0.0, 1.0);
}

float D_GGX(float alphaRoughness, float NdotH)
{
  float alphaRoughnessSq = alphaRoughness * alphaRoughness;
  float denom = NdotH * NdotH * (alphaRoughnessSq - 1.0) + 1.0;
  return alphaRoughnessSq / (kPi * denom * denom);
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

// https://google.github.io/filament/Filament.html#materialsystem/specularbrdf/geometricshadowing(specularg)
float smithGGXorrelatedMasking(float alphaRoughness, float AnyDotAny)
{
  float alphaRoughnessSq = alphaRoughness * alphaRoughness;
  return 0.5 *
    (sqrt(alphaRoughnessSq + (1 - alphaRoughnessSq) * AnyDotAny * AnyDotAny) / AnyDotAny - 1);
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
  return color / kPi;
}

void main()
{
  const float roughness = params.roughness;

  const float alphaRoughness = roughness * roughness;

  const vec3 pointToLight = -normalize(shadowCastingDirLight.direction);

  const vec3 fromPosToCamera = normalize(cameraWorldPosition.xyz - pos); // V
  const vec3 fromPosToLight = normalize(pointToLight);                   // L
  const vec3 surfaceNormal = normalize(normal);                          // N
  const vec3 halfVector = normalize(fromPosToLight + fromPosToCamera);   // H

  const float VdotH = clampedDot(fromPosToCamera, halfVector);
  const float HdotL = clampedDot(halfVector, fromPosToLight);
  const float NdotL = clampedDot(surfaceNormal, fromPosToLight);
  const float NdotV = clampedDot(surfaceNormal, fromPosToCamera) + 0.00001;
  const float NdotH = clampedDot(surfaceNormal, halfVector) + 0.00001;

  const vec3 reflectedDir = reflect(-fromPosToCamera, normal);
  vec3 reflection = texture(skybox, reflectedDir).rgb * params.reflectionStrength;

  const vec4 lightSpacePos = lightProjView * vec4(pos, 1.0);
  const vec3 lightSpaceNDCPos = lightSpacePos.xyz / lightSpacePos.w;

  const vec2 shadowTexCoord = lightSpaceNDCPos.xy * 0.5 + vec2(0.5);

  const bool outOfView =
    (shadowTexCoord.x < 0.0001 || shadowTexCoord.x > 0.9999 || shadowTexCoord.y < 0.0001 ||
     shadowTexCoord.y > 0.9999);

  const float lightDepth = textureLod(gShadow, shadowTexCoord, 0).x + 0.0005;

  const float shadow = ((lightSpaceNDCPos.z < lightDepth) || outOfView) ? 0.0 : 1.0;

  const vec3 sunIrradiance = shadowCastingDirLight.intensity * shadowCastingDirLight.color;

  vec3 f0 = vec3(0.02);
  vec3 frensel = frenselSchlick(f0, NdotV);
  vec3 diffuse = diffuseBrdf(reflection);

  vec3 specular = sunIrradiance * NdotL * BRDFSpecular_GGX(alphaRoughness, NdotL, NdotV, NdotH);

  vec4 displacementAndFoam = texture(heightMap, texCoord);
  float height = max(0.0, displacementAndFoam.y);

  // fake subsurface scattering
  // see
  // https://gpuopen.com/gdc-presentations/2019/gdc-2019-agtd6-interactive-water-simulation-in-atlas.pdf
  // p.52
  float k1 = params.wavePeakScatterStrength * height *
    pow(clampedDot(pointToLight, -fromPosToCamera), 4.0) *
    pow(0.5 - 0.5 * dot(pointToLight, surfaceNormal), 3.0);
  float k2 = params.scatterStrength * pow(NdotV, 2.0);
  float k3 = params.scatterShadowStrength * NdotL;
  float k4 = params.bubbleDensity;

  float lightMask = smithGGXorrelatedMasking(alphaRoughness, NdotL);

  vec3 scatter = (k1 + k2) * params.scatterColor.xyz * sunIrradiance * 1 / (1 + lightMask);
  scatter +=
    k3 * params.scatterColor.xyz * sunIrradiance + k4 * params.bubbleColor.xyz * sunIrradiance;

  vec3 brdf = max(vec3(0.0), mix(scatter, diffuse, frensel) + specular * (1.0 - shadow));

  float foam = clamp(displacementAndFoam.w, 0.0, 1.0);

  fragColor = vec4(mix(brdf, params.foamColor.xyz, foam) * (1.0 - 0.7 * shadow), 1.0);
  // fragColor = vec4(params.scatterColor.xyz, 1);
}
