#ifndef TERRAIN_PARAMS_H_INCLUDED
#define TERRAIN_PARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"

struct TerrainParams
{
  shader_uvec2 extent;
  shader_uvec2 chunk;
  shader_uvec2 terrainInChunks;
  shader_vec2 terrainOffset;

  shader_float heightAmplifier;
  shader_float heightOffset;
};


#endif // TERRAIN_PARAMS_H_INCLUDED
