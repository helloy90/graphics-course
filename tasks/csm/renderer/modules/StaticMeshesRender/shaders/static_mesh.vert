#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : require

#include "unpack_attributes_baked.glsl"

layout(location = 0) in vec4 vPosNorm;
layout(location = 1) in vec4 vTexCoordAndTang;

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

layout(set = 0, binding = 0) readonly buffer materials_t
{
  Material materials[];
};
layout(set = 0, binding = 1) uniform sampler2D textures[];

layout(set = 1, binding = 0) readonly buffer relems_t
{
  RenderElement relems[];
};

layout(std140, set = 1, binding = 1) readonly buffer instance_matrices_t
{
  mat4 instanceMatrices[];
};

layout(set = 1, binding = 2) readonly buffer draw_instance_indices_t
{
  uint drawInstanceIndices[];
};

layout(push_constant) uniform proj_view_t
{
  mat4 projView;
};

layout(location = 0) out VS_OUT
{
  vec3 wPos;
  // vec3 wNorm;
  // vec4 wTangent;
  // vec3 wBitangent;
  vec3 wNormOut;
  vec2 texCoord;
  flat uint relemIdx;
}
vOut;


out gl_PerVertex
{
  vec4 gl_Position;
};

void main(void)
{
  vOut.relemIdx = gl_DrawID;

  mat4 currentModelMatrix = instanceMatrices[drawInstanceIndices[gl_InstanceIndex]];

  RenderElement currentRelem = relems[vOut.relemIdx];

  const vec4 wNorm = decode_normal(floatBitsToUint(vPosNorm.w));
  vec4 wTang = decode_normal(floatBitsToUint(vTexCoordAndTang.z));

  vOut.wPos = (currentModelMatrix * vec4(vPosNorm.xyz, 1.0f)).xyz;
  vec3 normalSpace = mat3(transpose(inverse(currentModelMatrix))) * wNorm.xyz;
  vec3 tangentSpace = mat3(transpose(inverse(currentModelMatrix))) * wTang.xyz;
  vec3 BitangentSpace = cross(normalSpace, tangentSpace) * wTang.w;
  vOut.texCoord = vTexCoordAndTang.xy;

  uint normalTextureIdx = materials[currentRelem.material].normalTexture;

  vec3 normal = texture(textures[nonuniformEXT(normalTextureIdx)], vOut.texCoord).rgb;
  vOut.wNormOut =
    normalize(normal.x * tangentSpace + normal.y * BitangentSpace + normal.z * normalSpace);

  gl_Position = projView * vec4(vOut.wPos, 1.0);
}
