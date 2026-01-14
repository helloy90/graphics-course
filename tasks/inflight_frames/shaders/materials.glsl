struct Material {
  vec3 ambient_color; // ambient reflection * ambient lighting
  vec3 diffuse_color; // diffuse reflection * light intensity
  vec3 specular_color; // specular reflection * specular light
  float reflection;
  float shininess;
  int texture_id; // if -1 then no texture
};

Material mGuard() {
  vec3 aCol = 0.4 * vec3(0.8);
  vec3 dCol = 0.3 * vec3(0.7);
  vec3 sCol = 0.2 * vec3(1, 1, 1);
  float ref = 0.01;
  float a = 1.;
  int tex_id = 1;

  return Material(aCol, dCol, sCol, ref, a, tex_id);
}

Material mHandle() {
  vec3 aCol = 0.5 * vec3(0.1);
  vec3 dCol = 0.5 * vec3(0.4);
  vec3 sCol = 0.6 * vec3(0);
  float ref = 0.0;
  float a = 1.;
  int tex_id = 1;

  return Material(aCol, dCol, sCol, ref, a, tex_id);
}

Material mBlade() {
  vec3 aCol = 1.5 * vec3(0.7, 0, 0);
  vec3 dCol = 0.6 * vec3(0.7, 0, 0);
  vec3 sCol = 1.0 * vec3(1, 1, 1);
  float ref = 1.0;
  float a = 50.;
  int tex_id = 0;

  return Material(aCol, dCol, sCol, ref, a, tex_id);
}

Material mPlane(vec3 p) {
  vec3 aCol = 0.4 * vec3(0.835, 1, 1);
  vec3 dCol = 0.3 * vec3(1.0);
  vec3 sCol = 0.0 * vec3(1.0);
  float ref = 0.2;
  float a = 1.0;
  int tex_id = -1;

  return Material(aCol, dCol, sCol, ref, a, tex_id);
}
