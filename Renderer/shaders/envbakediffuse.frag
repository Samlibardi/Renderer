#version 450
#extension GL_KHR_vulkan_glsl: enable

const float PI = 3.14159265359;

layout(location=0) in vec3 position;

layout(set=0, binding=0) uniform samplerCube envSampler;

layout(location=0) out vec3 outColor;

void main() {
    vec3 normal = normalize(position);
    vec3 up = vec3(0.0f, 1.0f, 0.0f);
    vec3 right = normalize(cross(up, normal));
    up = normalize(cross(normal, right));

    const int nSamples = 24;
    float stepRads = 2 * PI / nSamples;

    vec3 irradiance = vec3(0.0f);

    for(int i = 0; i < nSamples; i++) {
        for (int j = 0; j < nSamples / 4; j++) {
            float phi = i * stepRads;
            float theta = j * stepRads;

            // spherical to cartesian (in tangent space)
            vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * normal; 

            irradiance += texture(envSampler, sampleVec).rgb * cos(theta) * sin(theta);
        }
    }

    irradiance = PI * irradiance * (1.0f / float(nSamples * nSamples / 4));
   
    outColor = irradiance;
}