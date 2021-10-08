#include "Camera.h"


glm::vec3 Camera::position() {
	return this->_position;
};

glm::mat4 Camera::viewMatrix() {
	this->mutex.lock();
	if (matrixDirty)
		this->rebuildViewMatrix();
	glm::mat4 m = this->_viewMatrix;
	this->mutex.unlock();
	return m;
};

std::tuple<glm::vec3, glm::mat4> Camera::positionAndMatrix() {
	this->mutex.lock();
	if (matrixDirty)
		this->rebuildViewMatrix();
	std::tuple<glm::vec3, glm::mat4> rv{ this->_position, this->_viewMatrix };
	this->mutex.unlock();
	return rv;
}

void Camera::move(glm::vec3 translation)
{
	this->mutex.lock();
	translation = glm::mat3(glm::inverse(this->_viewMatrix)) * translation;

	this->_position += translation;
	this->_viewMatrix = this->_viewMatrix * glm::translate(-translation);
	this->mutex.unlock();
}

void Camera::pan(glm::vec2 direction) {
	this->move(glm::vec3{ direction, 0.0f });
}

void Camera::dolly(float distance) {
	this->move(glm::vec3{ glm::vec2{0.0f}, -distance });
}

void Camera::tilt(glm::vec2 yawPitch) {
	this->mutex.lock();
	this->_eulerRotation += glm::vec3(0.0f, yawPitch.y, yawPitch.x);
	this->matrixDirty = true;
	this->mutex.unlock();
}

void Camera::rebuildViewMatrix() {
	this->_viewMatrix = glm::rotate(-this->_eulerRotation.x, glm::vec3{ 0.0f, 0.0f, 1.0f }) * glm::rotate(-this->_eulerRotation.y, glm::vec3{ 1.0f, 0.0f, 0.0f }) * glm::rotate(-this->_eulerRotation.z, glm::vec3{ 0.0f, 1.0f, 0.0f }) * glm::translate(-this->_position);
	this->matrixDirty = false;
}