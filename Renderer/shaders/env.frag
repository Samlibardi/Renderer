#version 450
#extension GL_KHR_vulkan_glsl: enable

layout(location=0) in vec3 position;

layout(set=0, binding=0) uniform samplerCube envSampler;

layout(location=0) out vec4 outColor;

void main() {
    vec3 color = texture(envSampler, normalize(position)).rgb;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));
   
    outColor = vec4(color, 1.0);
}