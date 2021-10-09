#pragma once
#include <shared_mutex>
#include <tuple>

#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>

class Camera
{
public:
	Camera() {};
	Camera(const glm::vec3 position, const glm::vec3 eulerRotation) : _position(position), _eulerRotation(eulerRotation) {};
	Camera(const Camera& rhs) : _position(rhs._position), _eulerRotation(rhs._eulerRotation), _viewMatrix(rhs._viewMatrix), matrixDirty(rhs.matrixDirty) {};
	Camera(Camera&& rhs) noexcept : _position(std::move(rhs._position)), _eulerRotation(std::move(rhs._eulerRotation)), _viewMatrix(std::move(rhs._viewMatrix)), matrixDirty(std::move(rhs.matrixDirty)) {};

	glm::vec3 position();
	glm::mat4 viewMatrix();
	std::tuple<glm::vec3, glm::mat4> positionAndMatrix();
	
	void move(glm::vec3 translation);
	void lookAt(glm::vec3 focusPoint);
	void dolly(float distance);
	void pan(glm::vec2 direction);
	void tilt(glm::vec2 yawPitch);

private:
	std::shared_mutex mutex;
	glm::vec3 _position{ 0.0f };
	glm::vec3 _eulerRotation{ 0.0f };
	glm::mat4 _viewMatrix{ 1.0f };

	void rebuildViewMatrix();

	bool matrixDirty = true;
};

