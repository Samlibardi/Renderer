#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inUv;

layout(location = 0) out vec3 outPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outTangent;
layout(location = 3) out vec3 outBitangent;
layout(location = 4) out vec2 outUv;

layout(push_constant) uniform constants {
    mat4 modelViewProj;
    mat4 model;
    vec4 cameraPos;
};

void main() {  
    gl_Position = modelViewProj * vec4(inPosition, 1.0f);
    outPosition = vec3(model*vec4(inPosition, 1.0f));
    outUv = inUv;
    outNormal = normalize(vec3(model * vec4(inNormal, 0.0f)));
    outTangent = normalize(vec3(model * vec4(inTangent, 0.0f)));
    outBitangent = normalize(vec3(model * vec4(inBitangent, 0.0f)));
}