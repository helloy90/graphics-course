#ifndef WATERRENDERPARAMS_H_INCLUDED
#define WATERRENDERPARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct WaterRenderParams {
    shader_vec4 color;
    shader_vec4 tipColor;
    shader_float tipAttenuation;
    shader_float roughness;
};

#endif // WATERRENDERPARAMS_H_INCLUDED