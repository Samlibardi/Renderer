#pragma once

#include <glm/vec3.hpp>

class PointLight
{
public:
	alignas(16) glm::vec3 point;
	alignas(16) glm::vec3 intensity;
};

