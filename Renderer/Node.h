#pragma once

#include <memory>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include "Animation.h"

class Node
{
public:
	Node(
		const glm::vec3& translation,
		const glm::quat& rotation,
		const glm::vec3& scale,
		const std::vector<std::shared_ptr<Node>>& children
	) : _translation(translation), _rotation(rotation), _scale(scale), children(children) {
		recalculateModel(glm::mat4{ 1.0f });
	};

	Node(
		const glm::mat4& matrix,
		const std::vector<std::shared_ptr<Node>>& children
	) : children(children), _model(matrix) {
		glm::vec3 s;
		glm::vec4 p;
		glm::decompose(matrix, this->_scale, this->_rotation, this->_translation, s, p);
		for (auto& child : this->children) {
			child->recalculateModel(matrix);
		}
	}

	glm::vec3 translation() { return this->_translation; }
	void setTranslation(glm::vec3 translation) { this->_translation = translation; this->modelDirty = true;}
	glm::quat rotation() { return this->_rotation; }
	void setRotation(glm::vec3 rotation) { this->_rotation = rotation; this->modelDirty = true;}
	glm::vec3 scale() { return this->_scale; }
	void setScale(glm::vec3 scale) { this->_scale = scale; this->modelDirty = true;}

	void setTransform(glm::vec3 translation, glm::quat rotation, glm::vec3 scale) {
		this->_translation = translation;
		this->_rotation = rotation;
		this->_scale = scale;
		this->modelDirty = true;
	}

	glm::mat4 modelMatrix();

	void setTranslationAnimation(const Animation<glm::vec3>& animation);
	void setTranslationAnimation(Animation<glm::vec3>&& animation);
	void setRotationAnimation(const Animation<glm::quat>& animation);
	void setRotationAnimation(Animation<glm::quat>&& animation);
	void setScaleAnimation(const Animation<glm::vec3>& animation);
	void setScaleAnimation(Animation<glm::vec3>&& animation);

	void setAnimationTime(float t);
	void recalculateModel(const glm::mat4& parentModel);

	bool isStatic() { return this->_isStatic; }
	void setStatic(bool isStatic);


private:
	std::vector<std::shared_ptr<Node>> children;

	glm::vec3 _translation{ 0.0f };
	glm::quat _rotation{};
	glm::vec3 _scale{ 1.0f };

	bool _isStatic = true;
	bool modelDirty = true;

	glm::mat4 _modelLocal;
	glm::mat4 _model;

	std::unique_ptr<Animation<glm::vec3>> translationAnimation;
	std::unique_ptr<Animation<glm::quat>> rotationAnimation;
	std::unique_ptr<Animation<glm::vec3>> scaleAnimation;
};

