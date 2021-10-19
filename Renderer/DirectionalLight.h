#pragma once

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/transform.hpp>

class DirectionalLight
{
public:
	glm::vec3 position;
	glm::vec3 intensity;
	glm::quat orientation;
	bool castShadows = true;
	bool staticShadowMapRendered = false;

	static inline const glm::mat4 projectionMatrix = glm::scale(glm::vec3{ 1.0f, -1.0f, 1.0f }) * glm::ortho<float>(-20.0f, 20.0f, -20.0f, 20.0f, 0.1f, 100.0f);

	glm::mat4 lightViewProjMatrix() const;
};

