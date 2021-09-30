#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtx/quaternion.hpp>

class Model
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


};

