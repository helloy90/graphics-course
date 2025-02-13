#ifndef UNIFORM_PARAMS_H_INCLUDED
#define UNIFORM_PARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct UniformParams
{
    shader_mat4 view;
    shader_mat4 proj;
    shader_mat4 projView;
    shader_vec3 cameraWorldPosition;
    shader_uint padding;
    shader_uvec2 extent;
    shader_uvec2 chunk;
    shader_uvec2 terrainInChunks;
};


#endif // UNIFORM_PARAMS_H_INCLUDED