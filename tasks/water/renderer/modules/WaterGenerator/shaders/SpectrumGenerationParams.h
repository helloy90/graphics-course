#ifndef WATERGENERATIONPARAMS_H_INCLUDED
#define WATERGENERATIONPARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct SpectrumGenerationParams {
    shader_vec2 windDirection;
    shader_float windSpeed;
    shader_float amplitude;
    shader_float lowCutoff;
    shader_float highCutoff;
    shader_uint seed;
    shader_uint patchSize;
};


#endif // WATERGENERATIONPARAMS_H_INCLUDED