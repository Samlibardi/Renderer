#version 450
#extension GL_KHR_vulkan_glsl: enable

const vec3 verts[] = {
    vec3(-1.0f, 1.0f, 0.0f),
    vec3(1.0f, 1.0f, 0.0f),
    vec3(-1.0f, -1.0f, 0.0f),

    vec3(1.0f, 1.0f, 0.0f),
    vec3(1.0f, -1.0f, 0.0f),
    vec3(-1.0f, -1.0f, 0.0f),
};

void main() {  
    vec4 pos = vec4(verts[gl_VertexIndex], 1.0f);
    gl_Position = pos;
}