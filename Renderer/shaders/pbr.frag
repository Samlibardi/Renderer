#version 450
#extension GL_KHR_vulkan_glsl: enable

#define PI radians(180)

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
layout(set=1, binding=2) uniform sampler2D normalSampler;
layout(set=1, binding=3) uniform sampler2D metalRoughSampler;
layout(set=1, binding=4) uniform sampler2D aoSampler;
layout(set=1, binding=5) uniform sampler2D emissiveSampler;

struct PointLight {
  vec3 position;
  vec3 intensity;
};

layout(set=0, binding=0) readonly buffer lightsBuffer {
   PointLight lights[];
};

layout(set=0, binding=1) uniform samplerCube envSpecSampler;
layout(set=0, binding=2) uniform samplerCube envDiffuseSampler;
layout(set=0, binding=3) uniform sampler2D brdfLUTSampler;


vec3 BRDF(vec3 radiance, vec3 L, vec3 N,  vec3 V, vec3 albedo, float roughness, float metallic, vec3 F0);
float DistributionGGX(vec3 N, vec3 H, float roughness);
float GeometrySchlickGGX(float NdotV, float roughness);
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);
vec3 fresnelSchlick(float cosTheta, vec3 F0);

void main()
{	
    vec3 albedo = (texture(albedoSampler, uv) * baseColorFactor).rgb;
    vec4 metalRoughMap = texture(metalRoughSampler, uv) * vec4(0.0f, roughnessFactor, metallicFactor, 0.0f);
    float metallic = clamp(metalRoughMap.b, 0.0f, 1.0f);
    float roughness = clamp(metalRoughMap.g, 0.05f, 1.0f);
    float ao = (texture(aoSampler, uv) * aoFactor).r;
    vec3 emissive = texture(emissiveSampler, uv).rgb * emissiveFactor.rgb;

    mat3 TBN = mat3(normalize(tangent), normalize(bitangent), normalize(normal));

    vec3 N = texture(normalSampler, uv).rgb;
    N = N * 2.0 - 1.0;
    N = normalize(TBN * N);

    vec3 V = normalize(vec3(cameraPos) - position);
    
    vec3 Rv = reflect(-V, N);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);
	           
    // reflectance equation
    vec3 Lo = vec3(0.0);

    for(int i = 0; i < lights.length(); i++) {
        vec3 lightPos = lights[i].position;
        vec3 lightIntensity = lights[i].intensity;
        
        // calculate per-light radiance
        vec3 L = normalize(lightPos - position);
        float d = distance(lightPos, position);
        float attenuation = 1.0 / (d*d);
        vec3 radiance = lightIntensity * attenuation;
        
        Lo += BRDF(radiance, L, N, V, albedo, roughness, metallic, F0);
    }
    
    //envmap specular
    const float NdotV = max(dot(N, V), 0.001);
    vec3 envSpecular = textureLod(envSpecSampler, Rv, roughness * textureQueryLevels(envSpecSampler)).rgb;
    vec2 envBRDF = textureLod(brdfLUTSampler, vec2(NdotV, roughness), 0).rg;
    vec3 F = fresnelSchlick(max(dot(N, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;	

    envSpecular = envSpecular * (kS * envBRDF.r + envBRDF.y);
    vec3 envDiffuse = kD * texture(envDiffuseSampler, N).rgb * albedo;

    vec3 ambient = (envDiffuse + envSpecular) * ao;
    vec3 color = ambient + Lo + emissive;
	
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));
   
    outColor = vec4(color, 1.0);
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
        float denominator = max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 0.000000001f);
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