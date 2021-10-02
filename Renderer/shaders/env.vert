#version 450
#extension GL_KHR_vulkan_glsl: enable

layout(location=0) out vec3 outPosition;

layout(push_constant) uniform constants {
    mat4 invviewproj;
};


const vec3 verts[] = {
    vec3(-1.0f, 1.0f, 1.0f),
    vec3(1.0f, 1.0f, 1.0f),
    vec3(-1.0f, -1.0f, 1.0f),

    vec3(1.0f, 1.0f, 1.0f),
    vec3(1.0f, -1.0f, 1.0f),
    vec3(-1.0f, -1.0f, 1.0f),
};

void main() {  
    vec4 pos = vec4(verts[gl_VertexIndex], 1.0f);
    outPosition = (invviewproj * pos).xyz;
    gl_Position = pos;
}