#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in vec2 inUv;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outColor;
layout(location = 3) out vec2 outUv;

layout(push_constant) uniform constants {
    mat4 modelViewProj;
    mat4 model;
    vec4 cameraPos;
    vec4 materialParams;
};

void main() {
    gl_Position = modelViewProj * vec4(inPosition, 1.0f);
    outPosition = vec3(model*vec4(inPosition, 1.0f));
    outColor = inColor;
    outNormal = vec3(model*vec4(inNormal, 0.0f));
    outUv = inUv;
}