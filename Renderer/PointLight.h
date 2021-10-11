#pragma once

#include <glm/glm.hpp>

class PointLight
{
public:
	glm::vec3 point;
	glm::vec3 intensity;
	bool staticMapRendered = false;
};

