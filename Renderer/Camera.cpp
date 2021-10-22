#include "Camera.h"

Camera::Camera(const Camera& rhs) : 
	_position(rhs._position),
	_eulerRotation(rhs._eulerRotation),
	_near(rhs._near),
	_far(rhs._far),
	_fovOrHeight(rhs._fovOrHeight),
	_aspectRatio(rhs._aspectRatio),
	projMatrixDirty(rhs.projMatrixDirty),
	viewMatrixDirty(rhs.viewMatrixDirty),
	viewProjMatrixDirty(rhs.viewProjMatrixDirty),
	cameraType(rhs.cameraType)
{
	if (!rhs.projMatrixDirty)
		this->_projMatrix = rhs._projMatrix;
	if (!rhs.viewMatrixDirty)
		this->_viewMatrix = rhs._viewMatrix;
	if (!rhs.viewProjMatrixDirty)
		this->_viewProjMatrix = rhs._viewProjMatrix;
}

Camera& Camera::operator=(const Camera& rhs) {
	this->_position = rhs._position;
	this->_eulerRotation = rhs._eulerRotation;
	this->_near = rhs._near;
	this->_far = rhs._far;
	this->_fovOrHeight = rhs._fovOrHeight;
	this->_aspectRatio = rhs._aspectRatio;
	this->projMatrixDirty = rhs.projMatrixDirty;
	this->viewMatrixDirty = rhs.viewMatrixDirty;
	this->viewProjMatrixDirty = rhs.viewProjMatrixDirty;
	this->cameraType = rhs.cameraType;
	if (!rhs.projMatrixDirty)
		this->_projMatrix = rhs._projMatrix;
	if (!rhs.viewMatrixDirty)
		this->_viewMatrix = rhs._viewMatrix;
	if (!rhs.viewProjMatrixDirty)
		this->_viewProjMatrix = rhs._viewProjMatrix;
	return *this;
}

Camera Camera::Perspective(const glm::vec3 position, const glm::vec3 eulerRotation, const float near, const float far, const float vfov, const float aspectRatio) {
	Camera c{};
	c.cameraType = CameraType::ePerspective;
	c._position = position;
	c._eulerRotation = eulerRotation;
	c._near = near;
	c._far = far;
	c._fovOrHeight = vfov;
	c._aspectRatio = aspectRatio;
	
	return c;
}

Camera Camera::Ortographic(const glm::vec3 position, const glm::vec3 eulerRotation, const float near, const float far, const float height, const float aspectRatio) {
	Camera c{};
	c.cameraType = CameraType::eOrthographic;
	c._position = position;
	c._eulerRotation = eulerRotation;
	c._near = near;
	c._far = far;
	c._fovOrHeight = height;
	c._aspectRatio = aspectRatio;

	return c;
}

glm::vec3 Camera::position() const {
	return this->_position;
};

glm::mat4 Camera::viewMatrix() const {
	this->mutex.lock();
	if (this->viewMatrixDirty)
		this->rebuildViewMatrixUnsafe();
	glm::mat4 m = this->_viewMatrix;
	this->mutex.unlock();
	return m;
};

glm::mat4 Camera::viewProjMatrix() const {
	this->mutex.lock();
	if (this->viewProjMatrixDirty)
		this->rebuildViewProjMatrixUnsafe();
	glm::mat4 m = this->_viewProjMatrix;
	this->mutex.unlock();
	return m;
};

std::tuple<glm::vec3, glm::mat4> Camera::positionAndMatrix() const {
	this->mutex.lock();
	if (this->viewProjMatrixDirty)
		this->rebuildViewProjMatrixUnsafe();

	std::tuple<glm::vec3, glm::mat4> rv{ this->_position, this->_viewProjMatrix };
	this->mutex.unlock();
	return rv;
}

void Camera::setPosition(glm::vec3 translation) {
	this->mutex.lock();

	this->_viewMatrix = this->_viewMatrix * glm::translate(this->_position-translation);
	this->_position = translation;
	this->viewProjMatrixDirty = true;
	this->mutex.unlock();
}

void Camera::move(glm::vec3 translation) {
	this->mutex.lock();
	translation = glm::mat3(glm::inverse(this->_viewMatrix)) * translation;

	this->_position += translation;
	this->_viewMatrix = this->_viewMatrix * glm::translate(-translation);
	this->viewProjMatrixDirty = true;
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
	this->_eulerRotation += glm::vec3{ yawPitch.y, yawPitch.x, 0.0f };
	this->viewMatrixDirty = true;
	this->viewProjMatrixDirty = true;
	this->mutex.unlock();
}

void Camera::rebuildViewMatrixUnsafe() const {
	this->_viewMatrix = glm::rotate(-this->_eulerRotation.x, glm::vec3{ 1.0f, 0.0f, 0.0f }) * glm::rotate(-this->_eulerRotation.y, glm::vec3{ 0.0f, 1.0f, 0.0f }) * glm::rotate(-this->_eulerRotation.z, glm::vec3{ 0.0f, 0.0f, 1.0f }) * glm::translate(-this->_position);
	this->viewMatrixDirty = false;
}
float Camera::near() const {
	return this->_near;
}
void Camera::setNear(float n) {
	this->mutex.lock();

	this->_near = n;
	this->projMatrixDirty = true;
	this->viewProjMatrixDirty = true;

	this->mutex.unlock();
}

float Camera::far() const {
	return this->_far;
}
void Camera::setFar(float f) {
	this->mutex.lock();

	this->_far = f;
	this->projMatrixDirty = true;
	this->viewProjMatrixDirty = true;

	this->mutex.unlock();
}

float Camera::vfov() const {
	return this->_fovOrHeight;
}
void Camera::setVFov(float fov) {
	this->mutex.lock();

	this->_fovOrHeight = fov;
	this->projMatrixDirty = true;
	this->viewProjMatrixDirty = true;

	this->mutex.unlock();
}

float Camera::hfov() const {
	return this->_aspectRatio * this->_fovOrHeight;
}

float Camera::aspectRatio() const {
	return this->_aspectRatio;
}
void Camera::setAspectRatio(float ar) {
	this->mutex.lock();

	this->_aspectRatio = ar;
	this->projMatrixDirty = true;
	this->viewProjMatrixDirty = true;

	this->mutex.unlock();
}

void Camera::rebuildProjMatrixUnsafe() const {
	switch (this->cameraType) {
	case CameraType::ePerspective:
		this->_projMatrix = glm::scale(glm::vec3{ 1.0f, -1.0f, 1.0f }) * glm::perspective(this->_fovOrHeight, this->_aspectRatio, this->_near, this->_far);
		break;
	case CameraType::eOrthographic:
		this->_projMatrix = glm::scale(glm::vec3{ 1.0f, -1.0f, 1.0f }) * glm::ortho(-this->_fovOrHeight * this->_aspectRatio / 2, this->_fovOrHeight * this->_aspectRatio / 2, -this->_fovOrHeight, this->_fovOrHeight, this->_near, this->_far);
		break;
	}
	this->projMatrixDirty = false;
}

void Camera::rebuildViewProjMatrixUnsafe() const {
	if (this->viewMatrixDirty)
		this->rebuildViewMatrixUnsafe();
	if (this->projMatrixDirty)
		this->rebuildProjMatrixUnsafe();

	this->_viewProjMatrix = this->_cropMatrix * this->_projMatrix * this->_viewMatrix;
	this->viewProjMatrixDirty = false;
}

std::array<glm::vec3, 8> Camera::getFrustumVertices() const {
	this->mutex.lock();
	if (this->viewProjMatrixDirty)
		this->rebuildViewProjMatrixUnsafe();
	const auto M = this->_viewProjMatrix;
	this->mutex.unlock();

	std::array<glm::vec3, 8> vertices{
		glm::vec3{ -1.0f, -1.0f, 0.0f },
		glm::vec3{ 1.0f, -1.0f, 0.0f },
		glm::vec3{ -1.0f, 1.0f, 0.0f },
		glm::vec3{ 1.0f, 1.0f, 0.0f },
		glm::vec3{ -1.0f, -1.0f, 1.0f },
		glm::vec3{ 1.0f, -1.0f, 1.0f },
		glm::vec3{ -1.0f, 1.0f, 1.0f },
		glm::vec3{ 1.0f, 1.0f, 1.0f },
	};

	for (auto& vert : vertices) {
		auto vh = glm::inverse(M) * glm::vec4(vert, 1.0f);
		vert = glm::vec3(vh) / vh.w;
	}

	return vertices;
}

//Source: http://www.cs.otago.ac.nz/postgrads/alexis/planeExtraction.pdf
std::array<glm::vec4, 6> getFrustumPlanesFromMatrix(const glm::mat4& M) {
	std::array<glm::vec4, 6> planes;

	//-X
	planes[0].x = M[0][3] + M[0][0];
	planes[0].y = M[1][3] + M[1][0];
	planes[0].z = M[2][3] + M[2][0];
	planes[0].w = M[3][3] + M[3][0];
	//+X
	planes[1].x = M[0][3] - M[0][0];
	planes[1].y = M[1][3] - M[1][0];
	planes[1].z = M[2][3] - M[2][0];
	planes[1].w = M[3][3] - M[3][0];
	//-Y
	planes[2].x = M[0][3] + M[0][1];
	planes[2].y = M[1][3] + M[1][1];
	planes[2].z = M[2][3] + M[2][1];
	planes[2].w = M[3][3] + M[3][1];
	//+Y
	planes[3].x = M[0][3] - M[0][1];
	planes[3].y = M[1][3] - M[1][1];
	planes[3].z = M[2][3] - M[2][1];
	planes[3].w = M[3][3] - M[3][1];
	//+Z
	planes[4].x = M[0][3] - M[0][2];
	planes[4].y = M[1][3] - M[1][2];
	planes[4].z = M[2][3] - M[2][2];
	planes[4].w = M[3][3] - M[3][2];
	//-Z
	planes[5].x = M[0][2];
	planes[5].y = M[1][2];
	planes[5].z = M[2][2];
	planes[5].w = M[3][2];

	return planes;
}

std::array<glm::vec4, 6> Camera::getFrustumPlanes() const
{
	this->mutex.lock();
	if (this->viewProjMatrixDirty)
		this->rebuildViewProjMatrixUnsafe();

	const auto M = this->_viewProjMatrix;
	this->mutex.unlock();

	return getFrustumPlanesFromMatrix(M);
}

std::array<glm::vec4, 6> Camera::getFrustumPlanesLocalSpace(glm::mat4 localMatrix) const
{
	this->mutex.lock();
	if (this->viewProjMatrixDirty)
		this->rebuildViewProjMatrixUnsafe();

	auto M = this->_viewProjMatrix;
	this->mutex.unlock();

	return getFrustumPlanesFromMatrix(M * localMatrix);
}

std::array<glm::vec3, 2> Camera::makeAABBFromVertices(std::vector<glm::vec3> vertices) const {
	this->mutex.lock();
	if (this->viewProjMatrixDirty)
		this->rebuildViewProjMatrixUnsafe();

	auto M = this->_viewProjMatrix;
	this->mutex.unlock();

	std::array<glm::vec3, 2> boundingBox = { glm::vec3(INFINITY), glm::vec3(-INFINITY) };
	for (auto& v : vertices) {
		glm::vec4 vhomog =  M * glm::vec4{ v, 1.0f };
		v = glm::vec3(vhomog) / vhomog.w;

		if (v.x < boundingBox[0].x) boundingBox[0].x = v.x;
		if (v.y < boundingBox[0].y) boundingBox[0].y = v.y;
		if (v.z < boundingBox[0].z) boundingBox[0].z = v.z;

		if (v.x > boundingBox[1].x) boundingBox[1].x = v.x;
		if (v.y > boundingBox[1].y) boundingBox[1].y = v.y;
		if (v.z > boundingBox[1].z) boundingBox[1].z = v.z;
	}

	return boundingBox;
}

void Camera::setCropMatrix(glm::mat4 cropMatrix) {
	this->_cropMatrix = cropMatrix;
	this->viewProjMatrixDirty = true;
}

void Camera::clearCropMatrix() {
	this->setCropMatrix(glm::mat4{ 1.0f });
}