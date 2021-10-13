#pragma once

#include <vector>
#include <map>

#include <glm/glm.hpp>

#include "Vertex.h"
#include "Texture.h"
#include "Animation.h"

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
	glm::vec3 translation() { return this->_translation; }
	void setTranslation(glm::vec3 translation) { this->_translation = translation; recalculateModel(); }
	glm::vec3 rotation() { return this->_rotation; }
	void setRotation(glm::vec3 rotation) { this->_rotation = rotation; recalculateModel(); }
	glm::vec3 scale() { return this->_scale; }
	void setScale(glm::vec3 scale) { this->_scale = scale; recalculateModel(); }

	void setTransform(glm::vec3 translation, glm::vec3 rotation, glm::vec3 scale) {
		this->_translation = translation;
		this->_rotation = rotation;
		this->_scale = scale;
		recalculateModel();
	}

	glm::vec3 barycenter() { return this->_barycenter; }

	bool isStatic = true;

	bool isIndexed = true;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	size_t firstVertex;
	size_t firstIndex;

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

	Animation<glm::vec3> translationAnimation{ {0.0f}, {this->_translation} };
	Animation<glm::vec3> rotationAnimation{ {0.0f}, {this->_rotation} };
	Animation<glm::vec3> scaleAnimation{ {0.0f}, {this->_scale} };

	void calculateTangents();
	void calculateBarycenter();
	void calculateBoundingBox();
	Mesh boundingBoxAsMesh() const;

	void setAnimationTime(float t);
	glm::mat4 modelMatrix();

private:
	glm::vec3 _translation{ 0.0f };
	glm::vec3 _rotation{ 0.0f };
	glm::vec3 _scale{ 1.0f };

	glm::vec3 _barycenter;

	void recalculateModel();
	glm::mat4 _model;
};

