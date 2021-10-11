#version 450
#extension GL_KHR_vulkan_glsl: enable

layout(set=0, binding=0, input_attachment_index=0) uniform subpassInput inputColor;

layout(push_constant) uniform pushConstants {
    float exposure;
    float gamma;
};

layout(location = 0) out vec4 outColor;


// filmic tonemap source: https://www.gdcvault.com/play/1012351/Uncharted-2-HDR

const float shoulderStr = 0.22f;
const float linStr = 0.30f;
const float linAngle = 0.10f;
const float toeStr = 0.20f;
const float toeNum = 0.01f;
const float toeDenom = 0.30f;
const float linWhitePoint = 11.2f;

float tone(float x) {
  return ((x*(shoulderStr*x + linAngle*linStr)+toeStr*toeNum)/(x*(shoulderStr*x+linStr)+toeStr*toeDenom)) - toeNum/toeDenom;
}

vec3 tone(vec3 v) {
    return vec3(tone(v.x), tone(v.y), tone(v.z));
}

void main() {
    vec3 color = subpassLoad(inputColor).rgb;

    color = tone(color)/tone(linWhitePoint);

    color = vec3(1.0) - exp(-color * exposure);
    color = pow(color, vec3(1.0 / gamma));

    outColor = vec4(color, 1.0f);
}