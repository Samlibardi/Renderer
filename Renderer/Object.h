#pragma once
#include <vector>
#include <map>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtx/quaternion.hpp>

#include "Vertex.h"

class Object
{
public:
	void setTranslation(glm::vec3 translation);
	void setRotation(glm::vec3 eulerRotation);
	void setRotation(glm::qua<float> quaternionRotation);
	void setScale(glm::vec3 scale);
	glm::mat4 getTransformMatrix();

private:
	glm::vec3 translation;
	glm::qua<float> rotation;
	glm::vec3 scale;

	bool transformDirty = true;
	glm::mat4 transform;

	std::vector<Vertex> vertices;
	std::vector<std::vector<uint32_t>> indicesByMaterial;

};