#version 460
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in VS_OUT {
    vec3 pos;
    vec3 normal;
};

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gMaterial;

void main() {
    gAlbedo = vec4(0.5, 0.5, 0.5, 1);
    gNormal = normal;
    gMaterial = vec4(0, 0.6, 0.05, 1);
}