#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include "Camera.h"

class DirectionalLight
{
public:
	glm::vec3 position;
	glm::vec3 intensity;
	glm::quat orientation;
	bool castShadows = true;
	bool staticShadowMapRendered = false;

	Camera lightViewCamera() const;
};

