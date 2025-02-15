#ifndef LIGHT_H_INCLUDED
#define LIGHT_H_INCLUDED

#include "cpp_glsl_compat.h"


struct Light
{
  shader_vec4 pos; // last coord is padding
  shader_vec3 color;
  float intensity;
};


#endif // LIGHT_H_INCLUDED
