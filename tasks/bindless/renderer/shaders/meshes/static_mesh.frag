#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable
// #extension GL_GOOGLE_include_directive : require

// #include "../MaterialRenderParams.h"

struct RenderElement {
    uint vertexOffset;
    uint indexOffset;
    uint indexCount;
    uint material;
};

struct Material { 
  vec4 baseColorFactor;
  float roughnessFactor;
  float metallicFactor;
  uint baseColorTexture;
  uint metallicRoughnessTexture;
  uint normalTexture;
  uint padding;
};

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gMaterial;

// layout(binding = 3) uniform sampler2D baseColorTexture;
// layout(binding = 4) uniform sampler2D normalTexture;
// layout(binding = 5) uniform sampler2D metallicRoughnessTexture;

layout(set = 0, binding = 0) uniform sampler2D textures[];
layout(set = 0, binding = 1) buffer materials_t {
  Material materials[];
};

// layout(push_constant) uniform params {
//   MaterialRenderParams materialParams;
// };

layout(set = 1, binding = 0) readonly buffer relems_t {
  RenderElement relems[];
};

layout(location = 0) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec4 wTangent;
  vec3 wBitangent;
  vec3 wNormOut;
  vec2 texCoord;
  uint relemIdx;
} surf;

void main()
{
  Material currentMaterial = materials[relems[surf.relemIdx].material];
  float currentLod = textureQueryLod(textures[nonuniformEXT(currentMaterial.baseColorTexture)], surf.texCoord).x;
  gAlbedo = textureLod(textures[nonuniformEXT(currentMaterial.baseColorTexture)], surf.texCoord, currentLod) * currentMaterial.baseColorFactor;
  gNormal = surf.wNormOut;
  gMaterial = texture(textures[nonuniformEXT(currentMaterial.metallicRoughnessTexture)], surf.texCoord);
  gMaterial.g *= currentMaterial.roughnessFactor;
  gMaterial.b *= currentMaterial.metallicFactor;
}
