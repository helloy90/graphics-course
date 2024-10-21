// --------- Constants ---------

const float kPi = 3.14159265359;

const mat3 kIdentityMat = mat3(
    vec3(1, 0, 0),
    vec3(0, 1, 0),
    vec3(0, 0, 1)
);

// --- Rotations ---

mat3 rotateX(in float angle) {
  float angle_cos = cos(angle);
  float angle_sin = sin(angle);
  return mat3(
      vec3(1, 0, 0),
      vec3(0, angle_cos, -angle_sin),
      vec3(0, angle_sin, angle_cos)
  );
}

mat3 rotateY(in float angle) {
  float angle_cos = cos(angle);
  float angle_sin = sin(angle);
  return mat3(
      vec3(angle_cos, 0, angle_sin),
      vec3(0, 1, 0),
      vec3(-angle_sin, 0, angle_cos)
  );
}

mat3 rotateZ(in float angle) {
  float angle_cos = cos(angle);
  float angle_sin = sin(angle);
  return mat3(
      vec3(angle_cos, -angle_sin, 0),
      vec3(angle_sin, angle_cos, 0),
      vec3(0, 0, 1)
  );
}

mat2 rotate2d(in float angle) {
  float angle_cos = cos(angle);
  float angle_sin = sin(angle);
  return mat2(
      vec2(angle_cos, angle_sin),
      vec2(-angle_sin, angle_cos)
  );
}