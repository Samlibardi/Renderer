#pragma once

#include <glm/glm.hpp>

class PointLight
{
public:
	alignas(16) glm::vec3 point;
	alignas(16) glm::vec3 intensity;
};

