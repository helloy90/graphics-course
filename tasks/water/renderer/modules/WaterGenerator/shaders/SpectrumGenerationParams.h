#ifndef WATERGENERATIONPARAMS_H_INCLUDED
#define WATERGENERATIONPARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct SpectrumGenerationParams {
    shader_vec2 windDirection;
    shader_float windSpeed;
    shader_float wavePeriod;
    shader_float gravity;
};


#endif // WATERGENERATIONPARAMS_H_INCLUDED