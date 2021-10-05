#pragma once

#include <vector>
#include <map>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "Vertex.h"
#include "Texture.h"

typedef struct {
	glm::vec4 baseColorFactor;
	glm::vec4 emissiveFactor;
	float normalScale;
	float metallicFactor;
	float roughnessFactor;
	float aoFactor;
} PBRInfo;

class Mesh
{
public:
	glm::vec3 translation{ 0.0f };
	glm::vec3 rotation{ 0.0f };
	glm::vec3 scale{ 1.0f };

	bool isIndexed = true;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	size_t firstVertex;
	size_t firstIndex;

	Texture albedoTexture{};
	Texture normalTexture{};
	Texture metalRoughnessTexture{};
	Texture emissiveTexture{};
	Texture aoTexture{};

	PBRInfo materialInfo{};

	vk::DescriptorSet descriptorSet;

	void calculateTangents();
};

