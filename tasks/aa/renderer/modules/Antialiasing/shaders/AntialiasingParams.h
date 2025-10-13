#ifndef ANTIALIASING_PARAMS_H_INCLUDED
#define ANTIALIASING_PARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct AntialiasingParams
{
  shader_mat4 currentProjView;
  shader_mat4 currentInvProjView;
  shader_mat4 previousProjView;
  shader_float depthCutoff;
  shader_float previousFrameUsage;
};


#endif // ANTIALIASING_PARAMS_H_INCLUDED
