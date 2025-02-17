#ifndef TERRAIN_GENERATION_PARAMS_H_INCLUDED
#define TERRAIN_GENERATION_PARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct TerrainGenerationParams
{
    shader_uvec2 extent;
    shader_uint numberOfSamples;
    shader_float persistence;
};


#endif // TERRAIN_GENERATION_PARAMS_H_INCLUDED