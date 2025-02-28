#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "../MaterialRenderParams.h"

// layout(location = 0) out vec4 out_fragColor;

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
  vec3 wTangent;
  vec2 texCoord;
} surf;

void main()
{
  // const vec3 wLightPos = vec3(10, 10, 10);
  // const vec3 surfaceColor = vec3(1.0f, 1.0f, 1.0f);

  // const vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);

  // const vec3 lightDir   = normalize(wLightPos - surf.wPos);
  // const vec3 diffuse = max(dot(surf.wNorm, lightDir), 0.0f) * lightColor;
  // const float ambient = 0.05;
  // out_fragColor.rgb = (diffuse + ambient) * surfaceColor;
  // out_fragColor.a = 1.0f;
  // vec2 texCoord_dx = dFdx(surf.texCoord);
  // vec2 texCoord_dy = dFdy(surf.texCoord);

  // if (length(texCoord_dx) <= 0.01) {
  //   texCoord_dx = vec2(1.0, 0.0);
  // }

  // if (length(texCoord_dy) <= 0.01) {
  //   texCoord_dy = vec2(0.0, 1.0);
  // }

  // vec3 reconstructTangent = 
  //   (texCoord_dy.t * dFdx(surf.wPos) - texCoord_dx.t * dFdy(surf.wPos)) / 
  //     (texCoord_dx.s * texCoord_dy.t - texCoord_dy.s * texCoord_dx.t);

  gAlbedo = texture(baseColorTexture, surf.texCoord) * materialParams.baseColorFactor;
  vec4 normal = 2 * texture(normalTexture, surf.texCoord) - 1;
  vec3 bitangent = normalize(cross(surf.wNorm, surf.wTangent));
  gNormal = normalize(bitangent * normal.x + surf.wTangent * normal.y + surf.wNorm * normal.z);
  gMaterial = texture(metallicRoughnessTexture, surf.texCoord);
  gMaterial.g *= materialParams.roughnessFactor;
  gMaterial.b *= materialParams.metallicFactor;
  // gAlbedo = vec4(1);
  // gNormal = surf.wNorm;
  // gMaterial = vec4(0, 0.5, 0, 1);
}
