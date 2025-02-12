#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D gAlbedo;
layout(binding = 1) uniform sampler2D gNormal;
layout(binding = 2) uniform sampler2D gDepth;

layout(push_constant) uniform resolution_t {
    uvec2 resolution;
};

struct Light {
    vec3 pos;
    vec3 color;
    float intensity;
};

void main() {
    vec2 texCoord = gl_FragCoord.xy / resolution;

    vec3 albedo = texture(gAlbedo, texCoord).rgb;
    vec3 normal = texture(gNormal, texCoord).xyz;
    vec3 depth = texture(gDepth, texCoord).x; //d32float

    Light light = {vec3(10, 20, 10), vec3(1), 1.0};

    vec3 ambient = vec3(0.1);

    vec3 lightDir = normalize(light.pos - vec3(uv_coord, -1));

    float normalLighting = clamp(dot(lightDir, normal), 0.0, 1.0);
    vec3 diffuse = albedo * normalLighting * light.color;

    vec3 color = (ambient + diffuse) * light.intensity;

    fragColor = vec4(color, 1);
}