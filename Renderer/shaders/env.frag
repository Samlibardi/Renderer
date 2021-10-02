#version 450
#extension GL_KHR_vulkan_glsl: enable

layout(location=0) in vec3 position;

layout(set=0, binding=1) uniform samplerCube envSampler;

layout(location=0) out vec3 outColor;

void main() {
    outColor = texture(envSampler, normalize(position)).rgb;
}