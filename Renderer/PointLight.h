#pragma once

#include <glm/vec3.hpp>

class PointLight
{
public:
	glm::vec3 point;
	glm::vec3 intensity;
	bool castShadows = true;
	bool staticShadowMapRendered = false;
};

