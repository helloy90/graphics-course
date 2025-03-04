#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../MaterialRenderParams.h"

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gMaterial;

layout(binding = 2) uniform sampler2D baseColorTexture;
layout(binding = 3) uniform sampler2D normalTexture;
layout(binding = 4) uniform sampler2D metallicRoughnessTexture;

layout(push_constant) uniform params {
  MaterialRenderParams materialParams;
};

layout(location = 0) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec4 wTangent;
  vec3 wBitangent;
  vec3 wNormOut;
  vec2 texCoord;
} surf;

void main()
{
  float currentLod = textureQueryLod(baseColorTexture, surf.texCoord).x;
  gAlbedo = textureLod(baseColorTexture, surf.texCoord, currentLod) * materialParams.baseColorFactor;
  gNormal = surf.wNormOut;
  gMaterial = texture(metallicRoughnessTexture, surf.texCoord);
  gMaterial.g *= materialParams.roughnessFactor;
  gMaterial.b *= materialParams.metallicFactor;
}
