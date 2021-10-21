#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inTangent;
layout(location = 3) in vec3 inBitangent;
layout(location = 4) in vec2 inUv;

layout(location = 0) out vec3 outWorldSpacePosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec3 outTangent;
layout(location = 3) out vec3 outBitangent;
layout(location = 4) out vec2 outUv;

layout(push_constant) uniform constants {
    mat4 modelViewProj;
    mat4 model;
};

layout(set=2, binding=0) uniform cameraData {
	vec4 cameraPos;
	mat4 viewProjectionMatrix;
	mat4 invViewProjectionMatrix;
};

void main() {
    gl_Position = modelViewProj * vec4(inPosition, 1.0f);
    vec4 p = model * vec4(inPosition, 1.0f);
    outWorldSpacePosition = p.xyz/p.w;
    outUv = inUv;
    outNormal = normalize((model * vec4(inNormal, 0.0f)).xyz);
    outTangent = normalize((model * vec4(inTangent, 0.0f)).xyz);
    outBitangent = normalize((model * vec4(inBitangent, 0.0f)).xyz);
}