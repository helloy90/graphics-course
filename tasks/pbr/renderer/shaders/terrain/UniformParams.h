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
  shader_vec3 cameraWorldPosition;

  shader_uint _padding;

  shader_uvec2 extent;
  shader_uvec2 chunk;
  shader_uvec2 terrainInChunks;
  shader_vec2 terrainOffset;

  shader_uint lightsAmount;
  shader_uint directionalLightsAmount;

  //attenuation constants
  shader_float constant;
  shader_float linear;
  shader_float quadratic;

  shader_float heightAmplifier;
  shader_float heightOffset;
};


#endif // UNIFORM_PARAMS_H_INCLUDED
