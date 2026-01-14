#extension GL_GOOGLE_include_directive : require

#include "materials.glsl"
#include "sdf.glsl"
#include "utils.glsl"

Surface sdOuterShell(in vec3 pos) { // unused
  Surface outer_shell = Surface(1, sdPlane(pos, vec3(0.0, 0.0, 1.0), 20.0), mPlane(pos));
  outer_shell = opSmoothUnion(Surface(1, sdPlane(pos, vec3(0.0, 1.0, 0.0), 1.0), mPlane(pos)), outer_shell, 0.9); // floor plane
  outer_shell = opSmoothUnion(Surface(1, sdPlane(pos, vec3(1.0, 0.0, 0.0), 20.0), mPlane(pos)), outer_shell, 0.9);
  outer_shell = opSmoothUnion(Surface(1, sdPlane(pos, vec3(0.0, 0.0, -1.0), 20.0), mPlane(pos)), outer_shell, 0.9);
  outer_shell = opSmoothUnion(Surface(1, sdPlane(pos, vec3(0.0, -1.0, 0.0), 20.0), mPlane(pos)), outer_shell, 0.9);
  outer_shell = opSmoothUnion(Surface(1, sdPlane(pos, vec3(-1.0, 0.0, 0.0), 20.0), mPlane(pos)), outer_shell, 0.9);
  return outer_shell;
}

Surface sdBlade(in vec3 pos) { 
  Surface upper_blade = Surface(2, sdBox(pos, vec3(0.03, 3, 0.05), vec3(0, 0.07, 3), rotateX(kPi/2.0)), mBlade());
  Surface lower_blade = Surface(2, sdTriPrism(pos, vec2(0.5, 3), vec3(0, 0.005, 3), vec3(1.1, 0.4, 0.05), rotateZ(kPi), kIdentityMat), mBlade());
  Surface tip = Surface(2, sdTriPrism(pos, vec2(0.5, 0.1), vec3(0, -0.023, 6.0), vec3(1, 0.4, 0.05), rotateZ(kPi), rotateX(-kPi/ 2.0)), mBlade());
  Surface intersector = Surface(2, sdSphere(pos, vec3(0, 4, 0), 7.15), mBlade());
  Surface blade_full = opSmoothUnion(upper_blade, lower_blade, 0.003);
  blade_full = opSmoothUnion(blade_full, tip, 0.005);
  blade_full = opIntersection(intersector, blade_full);
  return blade_full;
}

Surface sdGuardHandle(in vec3 pos) {
  mat3 guard_scale = mat3(
      vec3(1, 0, 0),
      vec3(0, 1, 0),
      vec3(0, 0, 1.5)
  );
  Surface guard = Surface(2, sdTor(pos, vec2(0.3, 0.1), vec3(0), rotateX(kPi / 2.0), guard_scale), mGuard());
  Surface handle = Surface(2, opRound(sdBox(pos, vec3(0.07, 0.8, 0.07), vec3(0, 0, -0.8), rotateX(kPi / 2.0)), 0.05), mHandle());
  return opSmoothUnion(guard, handle, 0.2);
}