#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 fragColor;

layout(binding = 0, r11f_g11f_b10f) uniform image2D renderImage;

layout(binding = 1) buffer distributionBuffer {
    float bins[];
};

layout(binding = 2) buffer histogramInfo {
    float binStepSize;
    float minWorldLuminance;
    float maxWorldLuminance;
    float minWorldBrightness;
    float maxWorldBrightness;
};

uint getBin(float luminance) {

    // luminance = clamp(luminance, minDisplayLuminance, maxDisplayLuminance); // ? for now (maybe need world min and max luminance)
    return uint(floor((log(luminance) - minWorldBrightness) / binStepSize)); 
}

void main() {
    vec3 color = texture(renderImage, gl_FragCoord).rgb;
    float luminance = 0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
    uint currentBin = getBin(luminance);
    float adjustedBrightness = 
        minDisplayBrightness 
        + (maxDisplayBrightness - minDisplayBrightness)
        * distributionBins[currentBin];    
    
    color = color * exp(adjustedBrightness) / luminance;
    fragColor = vec4(color, 1);
}