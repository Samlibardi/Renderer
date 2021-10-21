#version 450
#extension GL_KHR_vulkan_glsl: enable

#define PI radians(180)

#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_MASK 1
#define ALPHA_MODE_BLEND 2

layout(location = 0) in vec3 worldSpacePosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec2 uv;

layout(location = 0) out vec4 outColor;

layout(set=1, binding=0) uniform materialInfo {
  vec4 baseColorFactor;
  vec4 emissiveFactor;
  float normalScale;
  float metallicFactor;
  float roughnessFactor;
  float aoFactor;
};
layout(set=1, binding=1) uniform sampler2D albedoSampler;
layout(set=1, binding=2) uniform sampler2D normalSampler;
layout(set=1, binding=3) uniform sampler2D metalRoughSampler;
layout(set=1, binding=4) uniform sampler2D aoSampler;
layout(set=1, binding=5) uniform sampler2D emissiveSampler;
layout(set=1, binding=6) uniform alphaInfo {
    int alphaMode;
    float alphaCutoff;
};

struct PointLight {
  vec3 position;
  vec3 intensity;
};

struct DirectionalLight {
  vec3 position;
  vec3 direction;
  vec3 intensity;
};

struct CSMSplit {
    mat4 viewproj;
    float depth;
};

layout(set=0, binding=0) readonly buffer pointLightsBuffer {
   PointLight pointLights[];
};

layout(set=0, binding=1) uniform directionalLightBuffer {
    DirectionalLight directionalLight;
};

layout(set=0, binding=2) uniform samplerCube envSamplers[2];
layout(set=0, binding=3) uniform sampler2D brdfLUTSampler;
layout(set=0, binding=4) uniform samplerCubeArrayShadow pointShadowMapsSampler;
layout(set=0, binding=5) uniform sampler2DArrayShadow directionalShadowMapsSampler;

layout(set=2, binding=1) readonly buffer csmSplitsBuffer {
    CSMSplit csmSplits[];
};

layout(set=2, binding=0) uniform cameraData {
	vec4 cameraPos;
	mat4 viewProjectionMatrix;
	mat4 invViewProjectionMatrix;
};

vec3 BRDF(vec3 radiance, vec3 L, vec3 N,  vec3 V, vec3 albedo, float roughness, float metallic, vec3 F0);
float DistributionGGX(vec3 N, vec3 H, float roughness);
float GeometrySchlickGGX(float NdotV, float roughness);
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);
vec3 fresnelSchlick(float cosTheta, vec3 F0);

const float far = 100.0f;
const float near = 0.1f;

void main()
{
    vec4 albedo = (texture(albedoSampler, uv) * baseColorFactor);
    if(alphaMode == ALPHA_MODE_MASK && albedo.a < alphaCutoff) {
        outColor = vec4(0.0f);
        return;
    }
    vec4 metalRoughMap = texture(metalRoughSampler, uv) * vec4(0.0f, roughnessFactor, metallicFactor, 0.0f);
    float metallic = clamp(metalRoughMap.b, 0.0f, 1.0f);
    float roughness = clamp(metalRoughMap.g, 0.05f, 1.0f);
    float ao = (texture(aoSampler, uv) * aoFactor).r;
    vec3 emissive = texture(emissiveSampler, uv).rgb * emissiveFactor.rgb;

    mat3 TBN = mat3(normalize(tangent), normalize(bitangent), normalize(normal));

    vec3 N = texture(normalSampler, uv).rgb;
    N = N * 2.0 - 1.0;
    N = normalize(TBN * N);

    vec3 V = normalize(vec3(cameraPos) - worldSpacePosition);
    
    vec3 Rv = reflect(-V, N);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo.rgb, metallic);
	        
    //output light accumulator
    vec3 Lo = vec3(0.0);

    //point lights
    for(int i = 0; i < pointLights.length(); i++) {

        vec3 lightPos = pointLights[i].position;
        vec3 lightIntensity = pointLights[i].intensity;

        float d = distance(lightPos, worldSpacePosition);

        float attenuation = 1.0 / (d*d);
        vec3 radiance = lightIntensity * attenuation;

        if(max(radiance.r, max(radiance.g, radiance.b)) <= 1e-3f) {
            continue;
        }

        vec3 L = lightPos - worldSpacePosition;

        vec3 absL = abs(L);

        float lightDepth = max(absL.x, max(absL.y, absL.z)) - 0.05;

        lightDepth = (far - far * near / lightDepth )/(far - near);

        float shadowTest = texture(pointShadowMapsSampler, vec4(-L, i), lightDepth);

        if(shadowTest < 1e-3)
            continue;

        L = normalize(L);
        Lo += shadowTest * BRDF(radiance, L, N, V, albedo.rgb, roughness, metallic, F0);
    }

    //directional light
    {
        CSMSplit csmSplit = csmSplits[0];
        int splitIndex = 0;
        for(int i = 1; i < csmSplits.length(); i++) {
            if(gl_FragCoord.z < csmSplits[i].depth)
                break;
            csmSplit = csmSplits[i];
            splitIndex = i;
        }

        vec4 directionalLightSpacePosition = csmSplits[splitIndex].viewproj * vec4(worldSpacePosition, 1.0f);

        float shadowTest = texture(directionalShadowMapsSampler, vec4(directionalLightSpacePosition.xy/2 + 0.5f, splitIndex, directionalLightSpacePosition.z));

        if(shadowTest > 1e-3) {
            vec3 L = directionalLight.direction;
            L = normalize(L);
            Lo += shadowTest * BRDF(directionalLight.intensity, L, N, V, albedo.rgb, roughness, metallic, F0);
        }
    }

    //envmap
    {
        const float NdotV = max(dot(N, V), 1e-3f);
        vec3 envSpecular = textureLod(envSamplers[0], Rv, roughness * textureQueryLevels(envSamplers[0])).rgb;
        vec2 envBRDF = textureLod(brdfLUTSampler, vec2(NdotV, roughness), 0).rg;
        vec3 F = fresnelSchlick(NdotV, F0);

        vec3 kS = F;
        vec3 kD = 1.0 - kS;
        kD *= 1.0 - metallic;	

        envSpecular = envSpecular * (kS * envBRDF.r + envBRDF.y);
        vec3 envDiffuse = kD * texture(envSamplers[1], N).rgb * albedo.rgb;

        Lo += (envDiffuse + envSpecular) * ao;
    }

    vec3 color = Lo + emissive;
	
    if(alphaMode == ALPHA_MODE_MASK)
        outColor = vec4(color, 1.0f);
    else
        outColor = vec4(color, albedo.a);
}

vec3 BRDF(vec3 radiance, vec3 L, vec3 N,  vec3 V, vec3 albedo, float roughness, float metallic, vec3 F0) {
        vec3 H = normalize(V + L);

        // cook-torrance brdf
        float NDF = DistributionGGX(N, H, roughness);        
        float G   = GeometrySmith(N, V, L, roughness);      
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;	  
        
        vec3 numerator    = NDF * G * F;
        float denominator = max(4.0 * max(dot(N, V), 1e-3f) * max(dot(N, L), 1e-3f), 1e-5f);
        vec3 specular     = numerator / denominator;
            
        // add to outgoing radiance Lo
        float NdotL = max(dot(N, L), 0.0);                
        return (kD * albedo / PI + specular) * radiance * NdotL; 
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = clamp(dot(N, H), 0.0, 1.0);
    float NdotH2 = NdotH*NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
	
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.001);
    float NdotL = max(dot(N, L), 0.001);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - clamp(cosTheta, 0.0, 1.0), 5.0);
}