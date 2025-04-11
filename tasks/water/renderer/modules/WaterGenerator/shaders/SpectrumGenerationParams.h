#ifndef WATERGENERATIONPARAMS_H_INCLUDED
#define WATERGENERATIONPARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct SpectrumGenerationParams {
    shader_vec2 windDirection;
    shader_float windSpeed;
    shader_float gravity;
    shader_float wavePeriod;
};


#endif // WATERGENERATIONPARAMS_H_INCLUDED