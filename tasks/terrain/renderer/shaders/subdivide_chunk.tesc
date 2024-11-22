#version 450
#extension GL_GOOGLE_include_directive : require

#include "UniformParams.h"
#include "utils.glsl"

layout (vertices = 4) out;

layout (location = 0) in uint instanceIndex[]; // array for some reason

layout (location = 0) out vec2 heightMapTextureCoordinate[];
layout (location = 1) out vec3 worldPosition[];

layout(binging = 0) uniform params {
  UniformParams uniformParams;
}

const uint currentInstanceIndex = instanceIndex[0] // actual instance index

const uint chunkAmount = uniformParams.terrainInChunks.x * uniformParams.terrainInChunks.y;

const float maxDistance = 512.0;
const float minDistance = 1.0;

const float maxTesselationLevel = 10.0;
const float minTesselationLevel = 1.0;

vec3 getPosition(uint vertex) {
  uvec2 coordsOfChunkInGrid = uvec2(currentInstanceIndex % terrainInChunks.x, currentInstanceIndex / terrainInChunks.x);
  uvec2 coordsOfVertexInChunk = uvec2(vertex % 2, vertex / 2);
  //start position is (0, 0, 0)
  return toTerrainCoords(vec3(coordsOfChunkInGrid + coordsOfVertexInChunk, 0) * );
}

vec3 getPositionInHeightMap(uint vertex) {

}

float getTesselationLevel(float dist) {

}

void main() {

}