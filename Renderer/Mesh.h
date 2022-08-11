#pragma once

#include <vector>
#include <map>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "Vertex.h"
#include "Texture.h"
#include "Node.h"

typedef struct {
	glm::vec4 baseColorFactor;
	glm::vec4 emissiveFactor;
	float normalScale;
	float metallicFactor;
	float roughnessFactor;
	float aoFactor;
} PBRInfo;

enum class AlphaMode {
	eOpaque,
	eMask,
	eBlend,
};

typedef struct AlphaInfo {
	AlphaMode alphaMode = AlphaMode::eOpaque;
	float alphaCutoff = 0.5f;
} AlphaInfo;

class Mesh {
public:
	glm::vec3 barycenter() { return this->_barycenter; }
	
	std::shared_ptr<Node> node;

	bool isIndexed = true;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	uint32_t firstVertexOffset;
	uint32_t firstIndexOffset;

	std::pair<glm::vec3, glm::vec3> boundingBox;

	Texture albedoTexture{};
	Texture normalTexture{};
	Texture metalRoughnessTexture{};
	Texture emissiveTexture{};
	Texture aoTexture{};

	PBRInfo materialInfo{};
	AlphaInfo alphaInfo{};

	bool hasDescriptorSet = false;
	vk::DescriptorSet descriptorSet{};

	void calculateTangents();
	void calculateBarycenter();
	void calculateBoundingBox();
	Mesh boundingBoxAsMesh() const;

private:
	glm::vec3 _barycenter;
};

