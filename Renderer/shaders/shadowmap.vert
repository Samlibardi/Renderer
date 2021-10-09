#version 450

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform constants {
    mat4 modelViewProj;
    mat4 model;
    vec4 cameraPos;
};

void main() {  
    gl_Position = modelViewProj * vec4(inPosition, 1.0f);
}