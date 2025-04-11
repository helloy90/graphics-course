#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "TerrainParams.h"
#include "terrain_utils.glsl"

layout (vertices = 4) out;

layout (location = 0) in uint instanceIndex[];

layout (location = 0) out vec2 heightMapTextureCoord[];
layout (location = 1) out vec3 worldPosition[];

layout (binding = 0) uniform params_t {
  TerrainParams params;
};

layout (binding = 1) uniform height_params_t {
  float heightAmplifier;
  float heightOffset;
};

layout (binding = 2) uniform sampler2D heightMap;

layout(push_constant) uniform push_constant_t {
    mat4 projView;
    vec4 cameraWorldPosition;
};

const float maxDistance = 256.0;
const float minDistance = 32.0;

const float maxTesselationLevel = 16.0;
const float minTesselationLevel = 4.0;

vec3 getPosition(uint vertex) {
  uint currentInstanceIndex = instanceIndex[0];
  uvec2 coordsOfChunkInGrid = uvec2(
    currentInstanceIndex % params.terrainInChunks.x, 
    currentInstanceIndex / params.terrainInChunks.x
  );
  uvec2 coordsOfVertexInChunk = uvec2(vertex / 2, vertex % 2);
  //start position is terrainOffset
  vec2 worldCoords = params.terrainOffset + vec2((coordsOfChunkInGrid + coordsOfVertexInChunk) * params.chunk);
  return toTerrainCoords(vec3(worldCoords, 0));
}

float getNearestDistanceFromCamera(vec3 first, vec3 second) {
  float firstDistance = distance(
    getHorizontalCoords(first),
    getHorizontalCoords(cameraWorldPosition.xyz)
  );
  float secondDistance = distance(
    getHorizontalCoords(second),
    getHorizontalCoords(cameraWorldPosition.xyz)
  );
  return min(firstDistance, secondDistance);
}

vec2 getPositionInHeightMap(vec3 position) {
  return 0.5 * vec2(getHorizontalCoords(position)) / params.extent + 0.5;
}

float getTesselationLevel(float dist) {
  float interpolation = smoothstep(minDistance, maxDistance, dist);
  return mix(maxTesselationLevel, minTesselationLevel, interpolation);
}

bool within(float left, float value, float right) {
    return (left <= value) && (value <= right);
}

bool isInsideViewFrustum(vec3 pos) {
  float height = (texture(heightMap, getPositionInHeightMap(pos)).x - heightOffset) * heightAmplifier;
  vec4 clipSpaceCoord = projView * vec4(pos.x, height, pos.z, 1.0);
  return within(-clipSpaceCoord.w, clipSpaceCoord.x, clipSpaceCoord.w) 
          || within(-clipSpaceCoord.w, clipSpaceCoord.y, clipSpaceCoord.w)
          || within(0.0, clipSpaceCoord.z, clipSpaceCoord.w);
}

bool isVisible(vec3 leftLower, vec3 leftUpper, vec3 rightLower, vec3 rightUpper) {
  bool visible = false;
  visible = visible
            || isInsideViewFrustum(leftLower)
            || isInsideViewFrustum(leftUpper)
            || isInsideViewFrustum(rightLower)
            || isInsideViewFrustum(rightUpper);

  return visible;
}

void main() {
  vec3 leftLower = getPosition(0);
  vec3 leftUpper = getPosition(1);
  vec3 rightLower = getPosition(2);
  vec3 rightUpper = getPosition(3);

  if (!isVisible(leftLower, leftUpper, rightLower, rightUpper)) {
    gl_TessLevelOuter[0] = 0;
    gl_TessLevelOuter[1] = 0;
    gl_TessLevelOuter[2] = 0;
    gl_TessLevelOuter[3] = 0;

    gl_TessLevelInner[0] = 0;
    gl_TessLevelInner[1] = 0;

    return;
  }

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