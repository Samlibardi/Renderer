#version 450
#extension GL_KHR_vulkan_glsl: enable

layout(set=0, binding=0, input_attachment_index=0) uniform subpassInput inputColor;

layout(push_constant) uniform pushConstants {
    float exposure;
    float gamma;
};

layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = subpassLoad(inputColor).rgb;

    color = vec3(1.0) - exp(-color * exposure);
    color = pow(color, vec3(1.0 / gamma));

    outColor = vec4(color, 1.0f);
}