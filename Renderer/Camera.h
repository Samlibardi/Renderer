#pragma once
#include <shared_mutex>
#include <tuple>
#include <vector>
#include <array>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtx/transform.hpp>

#undef near
#undef far

class Camera
{
public:
	enum class CameraType {
		ePerspective,
		eOrthographic,
	};

	Camera() {};
	Camera(const Camera& rhs);
	Camera& operator=(const Camera& rhs);

	static Camera Perspective(const glm::vec3 position, const glm::vec3 eulerRotation, const float near, const float far, const float vfov, const float aspectRatio);
	static Camera Ortographic(const glm::vec3 position, const glm::vec3 eulerRotation, const float near, const float far, const float height, const float aspectRatio);

	glm::vec3 position() const;
	glm::mat4 viewMatrix() const;
	glm::mat4 viewProjMatrix() const;
	std::tuple<glm::vec3, glm::mat4> positionAndMatrix() const;
	
	void setPosition(glm::vec3 translation);
	void move(glm::vec3 translation);
	void lookAt(glm::vec3 focusPoint);
	void dolly(float distance);
	void pan(glm::vec2 direction);
	void tilt(glm::vec2 yawPitch);

	float near() const;
	float far() const;
	float vfov() const;
	float hfov() const;
	float aspectRatio() const;

	void setNear(float n);
	void setFar(float f);
	void setVFov(float fov);
	void setAspectRatio(float ar);

	void setCropMatrix(glm::mat4 cropMatrix);
	void clearCropMatrix();

	std::array<glm::vec3, 8> getFrustumVertices() const;
	std::array<glm::vec4, 6> getFrustumPlanes() const;
	std::array<glm::vec4, 6> getFrustumPlanesLocalSpace(glm::mat4 localMatrix) const;
	std::array<glm::vec3, 2> makeAABBFromVertices(std::vector<glm::vec3> vertices) const;

private:
	mutable std::shared_mutex mutex{};
	glm::vec3 _position{ 0.0f };
	glm::vec3 _eulerRotation{ 0.0f };

	mutable glm::mat4 _viewMatrix{ 1.0f };
	mutable bool viewMatrixDirty = true;

	mutable glm::mat4 _projMatrix{ 1.0f };
	mutable bool projMatrixDirty = true;

	mutable glm::mat4 _viewProjMatrix{ 1.0f };
	mutable bool viewProjMatrixDirty = true;

	glm::mat4 _cropMatrix{ 1.0f };

	CameraType cameraType = CameraType::ePerspective;

	float _near = 0.1f;
	float _far = 100.0f;
	float _fovOrHeight = 35.0f;
	float _aspectRatio = 16.0f/9.0f;

	void rebuildViewMatrixUnsafe() const;
	void rebuildProjMatrixUnsafe() const;
	void rebuildViewProjMatrixUnsafe() const;
};

