#version 450

layout(quads, )

layout(push_constant) uniform params_t
{
  mat4 projView;
  vec3 cameraWorldPosition;
} params;

const float heightAmplifier = 10.0;

void main() {
    
}