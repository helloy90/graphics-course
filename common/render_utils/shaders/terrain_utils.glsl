#ifndef TERRAIN_UTILS_GLSL_INCLUDED
#define TERRAIN_UTILS_GLSL_INCLUDED


vec4 toTerrainCoords(vec4 vector) {
    return vector.xzyw;
}

vec3 toTerrainCoords(vec3 vector) {
    return vector.xzy;
}

vec2 getHorizontalCoords(vec4 vector) {
    return vector.xz;
}

vec2 getHorizontalCoords(vec3 vector) {
    return vector.xz;
}

vec3 interpolate4Vert2D(vec3 leftLower, vec3 leftUpper, vec3 rightLower, vec3 rightUpper, float u, float v) {
  return leftLower * (1.0 - u) * (1.0 - v) 
          + leftUpper * (1.0 - u) * v
          + rightLower * u * (1.0 - v)
          + rightUpper * u * v;
}

vec2 interpolate4Vert2D(vec2 leftLower, vec2 leftUpper, vec2 rightLower, vec2 rightUpper, float u, float v) {
  return leftLower * (1.0 - u) * (1.0 - v) 
          + leftUpper * (1.0 - u) * v
          + rightLower * u * (1.0 - v)
          + rightUpper * u * v;
}


#endif // TERRAIN_UTILS_GLSL_INCLUDED