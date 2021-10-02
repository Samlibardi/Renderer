#version 450
#extension GL_KHR_vulkan_glsl: enable

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec2 uv;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform constants {
    mat4 modelViewProj;
    mat4 model;
    vec4 cameraPos;
};

layout(set=1, binding=0) uniform materialInfo {
  vec4 baseColorFactor;
  vec4 emissiveFactor;
  float normalScale;
  float metallicFactor;
  float roughnessFactor;
  float aoFactor;
};
layout(set=1, binding=1) uniform sampler2D albedoSampler;


struct PointLight {
  vec3 position;
  vec3 intensity;
};

layout(binding=1) readonly buffer lightsBuffer {
   PointLight lights[];
};

void main() {
    vec3 N = normalize(normal);
    vec3 V = normalize(vec3(cameraPos) - position);

    vec3 albedo =  texture(albedoSampler, uv).rgb;

    vec3 Lo = vec3(0.0f);
    
    for(int i = 0; i < lights.length(); i++) {
        float d = distance(position, lights[i].position);
        vec3 L = normalize(lights[i].position - position);
        vec3 R = normalize(2 * (dot(L, N) * N) - L);

        vec3 lightIntensityAtt = lights[i].intensity/(d*d);
        vec3 diffuse =  0.7f * lightIntensityAtt * max(dot(L, N), 0.0f) *  albedo;
        vec3 specular = 0.3f * lightIntensityAtt * pow(max(dot(R, V),0.0f), 16);
        
        Lo += diffuse + specular;
    }
    
    vec3 ambient = 0.01f * albedo;
    Lo += ambient;

    Lo = Lo/ (Lo + vec3(1.0));
    Lo = pow(Lo, vec3(1.0/2.2));  
    outColor = vec4(Lo, 1.0f);
}