#include "DirectionalLight.h"
 
Camera DirectionalLight::lightViewCamera() const {
	return Camera::Ortographic(this->position, glm::eulerAngles(this->orientation), 0.1f, 200.0f, 10.0f, 1.0f);
}