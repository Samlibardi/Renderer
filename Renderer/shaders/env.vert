#version 450
#extension GL_KHR_vulkan_glsl: enable

layout(location=0) out vec3 outPosition;

layout(set=1, binding=0) uniform cameraData {
	vec4 cameraPos;
	mat4 viewMatrix;
	mat4 viewProjectionMatrix;
	mat4 invViewProjectionMatrix;
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
    outPosition = (invViewProjectionMatrix * pos).xyz;
    gl_Position = pos;
}