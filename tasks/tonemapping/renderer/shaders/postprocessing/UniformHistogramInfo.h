#ifndef UNIFORM_HISTOGRAM_INFO_H_INCLUDED
#define UNIFORM_HISTOGRAM_INFO_H_INCLUDED

#include "cpp_glsl_compat.h"
#include <glm/fwd.hpp>


struct UniformHistogramInfo
{
    shader_float binStepSize;
    shader_float multiplier;
    glm::int32 intMinWorldLuminance;
    glm::int32 intMaxWorldLuminance;
    shader_float minWorldLuminance;    // relative
    shader_float maxWorldLuminance;    // relative
    shader_float minWorldBrightness;
    shader_float maxWorldBrightness;
};


#endif // UNIFORM_HISTOGRAM_INFO_H_INCLUDED