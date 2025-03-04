#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) in VS_OUT {
    vec3 pos;
    vec3 normal;
};

layout (location = 0) out vec4 gAlbedo;
layout (location = 1) out vec3 gNormal;
layout (location = 2) out vec4 gMaterial;

void main() {
    // vec3 lightPos = vec3(10, 20, 10);
    // float lightIntensity = 1.0;

    // vec3 ambient = vec3(0.1);
    // vec3 diffuseColor = vec3(0.5);

    // vec3 lightDir = normalize(lightPos - pos);

    // float normalLighting = clamp(dot(lightDir, normal), 0.0, 1.0);
    // vec3 diffuse = diffuseColor * normalLighting;

    // vec3 color = (ambient + diffuse) * lightIntensity;

    // fragColor = vec4(color, 1);

    gAlbedo = vec4(0.5, 0.5, 0.5, 1);
    gNormal = normal;
    gMaterial = vec4(0, 0.6, 0.05, 1);
}