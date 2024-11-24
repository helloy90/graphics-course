#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "UniformParams.h"
#include "utils.glsl"

layout (vertices = 4) out;

layout (location = 0) in uint instanceIndex[];

layout (location = 0) out vec2 heightMapTextureCoord[];
layout (location = 1) out vec3 worldPosition[];

layout (binding = 0) uniform params {
  UniformParams uniformParams;
};

// const uint currentInstanceIndex = ; // actual instance index

const float maxDistance = 512.0;
const float minDistance = 1.0;

const float maxTesselationLevel = 20.0;
const float minTesselationLevel = 1.0;

vec3 getPosition(uint vertex) {
  uvec2 coordsOfChunkInGrid = uvec2(
    instanceIndex[0] % uniformParams.terrainInChunks.x, 
    instanceIndex[0] / uniformParams.terrainInChunks.x
  );
  uvec2 coordsOfVertexInChunk = uvec2(vertex / 2, vertex % 2);
  //start position is (0, 0, 0)
  uvec2 worldCoords = (coordsOfChunkInGrid + coordsOfVertexInChunk) * uniformParams.chunk;
  return toTerrainCoords(vec3(worldCoords, 0));
}

float getNearestDistanceFromCamera(vec3 first, vec3 second) {
  float firstDistance = distance(
    getHorizontalCoords(first),
    getHorizontalCoords(uniformParams.cameraWorldPosition)
  );
  float secondDistance = distance(
    getHorizontalCoords(second),
    getHorizontalCoords(uniformParams.cameraWorldPosition)
  );
  return min(firstDistance, secondDistance);
}

vec2 getPositionInHeightMap(vec3 position) {
  float x = smoothstep(0.0, uniformParams.extent.x, position.x);
  float y = smoothstep(0.0, uniformParams.extent.y, position.z); // because in terrain coords
  return vec2(x, y);
}

float getTesselationLevel(float dist) {
  float interpolation = smoothstep(minDistance, maxDistance, dist);
  return mix(maxTesselationLevel, minTesselationLevel, interpolation);
}

void main() {
  vec3 leftLower = getPosition(0);
  vec3 leftUpper = getPosition(1);
  vec3 rightLower = getPosition(2);
  vec3 rightUpper = getPosition(3);

  vec3 save = getPosition(gl_InvocationID);
  worldPosition[gl_InvocationID] = save;
  heightMapTextureCoord[gl_InvocationID] = getPositionInHeightMap(save);

  gl_TessLevelOuter[0] = getTesselationLevel(getNearestDistanceFromCamera(leftLower, leftUpper));
  gl_TessLevelOuter[1] = getTesselationLevel(getNearestDistanceFromCamera(leftLower, rightLower));
  gl_TessLevelOuter[2] = getTesselationLevel(getNearestDistanceFromCamera(rightLower, rightUpper));
  gl_TessLevelOuter[3] = getTesselationLevel(getNearestDistanceFromCamera(leftUpper, rightUpper));

  gl_TessLevelInner[0] = max(gl_TessLevelOuter[1], gl_TessLevelOuter[3]);
  gl_TessLevelInner[1] = max(gl_TessLevelOuter[0], gl_TessLevelOuter[2]);
}