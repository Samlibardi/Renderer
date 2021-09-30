#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec4 outColor;

const vec3 lightPos = {10.0f, 120.0f, 90.0f};
const vec3 lightIntensity = {900.0f,850.0f,700.0f};

layout(push_constant) uniform constants {
    mat4 modelViewProj;
    mat4 model;
    vec4 cameraPos;
};

layout(binding=0) uniform sampler2D texSampler;

void main() {
    float d = distance(position, lightPos);
    vec3 L = normalize(lightPos - position);
    vec3 N = normalize(normal);
    vec3 R = normalize(2 * (dot(L, N) * N) - L);
    vec3 V = normalize(vec3(cameraPos) - position);

    vec3 lightIntensityAtt = lightIntensity/d/d;
    
    vec3 ambient = 0.2f * color;
    vec3 diffuse =  lightIntensityAtt * max(dot(L, N), 0.0f) * color;
    vec3 specular = lightIntensityAtt * pow(max(dot(R, V),0.0f),16);

    outColor = vec4(diffuse + ambient + specular, 1.0f);

    outColor = texture(texSampler, uv);
}