#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) buffer histogramBuffer {
    int bins[];
};