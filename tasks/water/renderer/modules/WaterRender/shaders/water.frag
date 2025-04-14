#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "WaterRenderParams.h"


layout (location = 0) in VS_OUT {
    vec3 pos;
    vec3 normal;
};

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gMaterial;

layout (binding = 1) uniform render_params_t {
    WaterRenderParams params;
};

void main() {
    vec4 waterAlbedo = params.color;
    vec4 tipColor = params.tipColor * clamp(pow(max(0.0, pos.y / 7.0), params.tipAttenuation), 0.0, 1.0);

    gAlbedo = vec4(waterAlbedo.xyz + tipColor.xyz, 1);
    gNormal = normal;
    gMaterial = vec4(0, params.roughness, 0, 1);
}