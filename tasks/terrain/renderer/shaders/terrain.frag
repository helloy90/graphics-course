#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

layout (binding = 1) uniform sampler2D heightMap;

layout (location = 0) in VS_OUT {
    vec3 pos;
    vec3 normal;
};

layout (location = 0) out vec4 fragColor;

void main() {
    vec3 light = vec3(10, 20, 10);

    vec3 ambientColor = vec3(0.1);
    vec3 diffuseColor = vec3(0.5);

    vec3 lightDir = normalize(light - pos);

    float normalLighting = clamp(dot(lightDir, normal), 0.0, 1.0);
    vec3 diffuse = diffuseColor * normalLighting;

    vec3 color = ambientColor + diffuseColor;

    fragColor = vec4(color, 1);
}