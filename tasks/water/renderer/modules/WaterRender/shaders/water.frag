#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "WaterRenderParams.h"


layout (location = 0) in VS_OUT {
    vec3 pos;
    vec3 normal;
    vec2 texCoord;
};

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gMaterial;

layout (binding = 1) uniform render_params_t {
    WaterRenderParams params;
};

layout (binding = 3) uniform sampler2D normalMap;

vec3 frenselSchlick(vec3 f0, float theta) {
    return f0 + (vec3(1.0) - f0) * pow(clamp(1.0 - abs(theta), 0.0, 1.0), 5.0);
}


void main() {

    gAlbedo = params.color;
    gNormal = normal;
    gMaterial = vec4(0, params.roughness, 0, 1);
}