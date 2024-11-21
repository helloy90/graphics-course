#version 450
layout (vertices = 4) out;

layout (location = 0) in uint instanceIndex[]; // array for some reason

layout (location = 0) out vec2 heightMapTextureCoordinate[];
layout (location = 1) out vec3 worldPosition[];

layout(push_constant) uniform params_t
{
  mat4 projView;
  vec3 cameraWorldPosition;
} params;



void main() {
    
}