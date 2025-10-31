#ifndef WATERPARAMS_H_INCLUDED
#define WATERPARAMS_H_INCLUDED

#include "cpp_glsl_compat.h"


struct WaterParams
{
  shader_uvec2 extent;
  shader_uvec2 chunk;
  shader_uvec2 waterInChunks;
  shader_vec2 waterOffset;
  // extrude the center for terrain
  shader_uvec2 extrusionInChunks;
  shader_float heightOffset;
};

#endif // WATERPARAMS_H_INCLUDED
