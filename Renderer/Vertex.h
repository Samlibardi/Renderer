#pragma once

#include <glm/vec3.hpp>
#include <glm/vec2.hpp>

class Vertex
{
public:
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec3 color = { 0.9f, 0.8f, 0.0f };
	glm::vec2 uv = { 0.0f, 0.0f };
};