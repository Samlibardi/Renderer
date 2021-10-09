#pragma once

#include <glm/glm.hpp>

class Vertex
{
public:
	alignas(16) glm::vec3 pos;
	alignas(16) glm::vec3 normal;
	alignas(16) glm::vec3 tangent = glm::vec3(0.0f);
	alignas(16) glm::vec3 bitangent = glm::vec3(0.0f);
	alignas(16) glm::vec2 uv = glm::vec3(0.0f);
};