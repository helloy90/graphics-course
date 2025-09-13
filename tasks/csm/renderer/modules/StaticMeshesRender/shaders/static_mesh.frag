#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

struct RenderElement
{
  uint vertexOffset;
  uint indexOffset;
  uint indexCount;
  uint material;
};

struct Material
{
  vec4 baseColorFactor;
  float roughnessFactor;
  float metallicFactor;
  uint baseColorTexture;
  uint metallicRoughnessTexture;
  uint normalTexture;
  uint _padding0;
  uint _padding1;
  uint _padding2;
};

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec3 gNormal;
layout(location = 2) out vec4 gMaterial;

layout(set = 0, binding = 0) uniform sampler2D textures[32];
layout(set = 0, binding = 1) readonly buffer materials_t
{
  Material materials[];
};

layout(set = 1, binding = 0) readonly buffer relems_t
{
  RenderElement relems[];
};

layout(location = 0) in VS_OUT
{
  vec3 wPos;
  // vec3 wNorm;
  // vec4 wTangent;
  // vec3 wBitangent;
  vec3 wNormOut;
  vec2 texCoord;
  flat uint relemIdx;
}
surf;


void main()
{
  Material currentMaterial = materials[relems[surf.relemIdx].material];
  float currentLod =
    textureQueryLod(textures[nonuniformEXT(currentMaterial.baseColorTexture)], surf.texCoord).x;
  gAlbedo =
    textureLod(
      textures[nonuniformEXT(currentMaterial.baseColorTexture)], surf.texCoord, currentLod) *
    currentMaterial.baseColorFactor;
  gNormal = surf.wNormOut;
  gMaterial =
    texture(textures[nonuniformEXT(currentMaterial.metallicRoughnessTexture)], surf.texCoord);
  gMaterial.g *= currentMaterial.roughnessFactor;
  gMaterial.b *= currentMaterial.metallicFactor;
}
