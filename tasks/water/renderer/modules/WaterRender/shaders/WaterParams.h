#ifndef WATERPARAMS_H_INCLUDED
#define WATERPARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct WaterParams {
    shader_uvec2 extent;
    shader_uvec2 chunk;
    shader_uvec2 waterInChunks;
    shader_vec2 waterOffset;
};

#endif // WATERPARAMS_H_INCLUDED