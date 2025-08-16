#ifndef UNIFORM_PARAMS_H_INCLUDED
#define UNIFORM_PARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"

struct UniformParams
{
  shader_mat4 view;
  shader_mat4 invView;
  shader_mat4 proj;
  shader_mat4 invProj;
  shader_mat4 projView;
  shader_mat4 invProjView;
  shader_mat4 invProjViewMat3;
  shader_vec3 cameraWorldPosition;

  shader_uint _padding;
};


#endif // UNIFORM_PARAMS_H_INCLUDED
