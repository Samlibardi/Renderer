#include "DirectionalLight.h"

glm::mat4 DirectionalLight::lightViewProjMatrix() const {
	glm::mat4 view = glm::mat4(glm::inverse(this->orientation)) * glm::translate(-this->position);
	return DirectionalLight::projectionMatrix * view;
}