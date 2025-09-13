#version 460
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in VS_OUT
{
  vec3 pos;
  vec2 texCoord;
};

layout(location = 0) out vec4 gAlbedo;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gMaterial;

layout(set = 0, binding = 1) uniform sampler2D normalMap;

void main()
{
  gAlbedo = vec4(0.5, 0.5, 0.5, 1);
  gNormal = texture(normalMap, texCoord);
  gMaterial = vec4(0, 1, 0.0, 1);
}
