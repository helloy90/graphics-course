#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "UniformParams.h"
#include "utils.glsl"

layout(quads, fractional_even_spacing, ccw) in;

layout (location = 0) in vec2 heightMapTextureCoord[];
layout (location = 1) in vec3 worldPosition[];

layout (location = 0) out VS_OUT {
  vec3 pos;
  vec3 normal;
};

layout (binding = 0) uniform params {
  UniformParams uniformParams;
};

layout (binding = 1) uniform sampler2D heightMap;

vec3 generateNormal(vec2 currentTexCoord) {
    float left = textureOffset(heightMap, currentTexCoord, ivec2(-1, 0)).x;
    float right = textureOffset(heightMap, currentTexCoord, ivec2(1, 0)).x;
    float up = textureOffset(heightMap, currentTexCoord, ivec2(0, 1)).x;
    float down = textureOffset(heightMap, currentTexCoord, ivec2(0, -1)).x;

    vec3 normal = normalize(vec3(left - right, 2.0, up - down));

    return normal;
}

const float heightAmplifier = 10.0;

void main() {

  float u = gl_TessCoord.x;
  float v = gl_TessCoord.y;

  vec3 leftLower = worldPosition[0];
  vec3 leftUpper = worldPosition[1];
  vec3 rightLower = worldPosition[2];
  vec3 rightUpper = worldPosition[3];

  vec3 currentVertex = interpolate4Vert2D(leftLower, leftUpper, rightLower, rightUpper, u, v);

  vec2 texLeftLower = heightMapTextureCoord[0];
  vec2 texLeftUpper = heightMapTextureCoord[1];
  vec2 texRightLower = heightMapTextureCoord[2];
  vec2 texRightUpper = heightMapTextureCoord[3];

  vec2 currentTexCoord = interpolate4Vert2D(texLeftLower, texLeftUpper, texRightLower, texRightUpper, u, v);

  currentVertex.y = texture(heightMap, currentTexCoord).x * heightAmplifier;

  pos = currentVertex;
  normal = generateNormal(currentTexCoord);

  gl_Position = uniformParams.projView * vec4(currentVertex, 1.0);
}