#include "Node.h"

#include <glm/gtx/transform.hpp>

glm::mat4 Node::modelMatrix() {
	return _model;
}

void Node::recalculateModel(const glm::mat4& parentModel) {
	if(this->modelDirty)
		this->_modelLocal = glm::translate(this->_translation) * glm::mat4_cast(this->_rotation) * glm::scale(this->_scale);
	this->modelDirty = false;

	this->_model = parentModel * this->_modelLocal;
	
	for (auto& child : this->children) {
		child->recalculateModel(this->_model);
	}
}

void Node::setAnimationTime(float t) {
	if (this->translationAnimation) {
		this->_translation = this->translationAnimation->valueAt(t);
		this->modelDirty = true;
	}
	if (this->rotationAnimation) {
		this->_rotation = this->rotationAnimation->valueAt(t);
		this->modelDirty = true;
	}
	if (this->scaleAnimation) {
		this->_scale = this->scaleAnimation->valueAt(t);
		this->modelDirty = true;
	}

	for (auto& child : this->children) {
		child->setAnimationTime(t);
	}
}

void Node::setTranslationAnimation(const Animation<glm::vec3>& animation) {
	this->translationAnimation = std::make_unique<Animation<glm::vec3>>(animation);
	this->setStatic(false);
}
void Node::setTranslationAnimation(Animation<glm::vec3>&& animation) {
	this->translationAnimation = std::make_unique<Animation<glm::vec3>>(std::move(animation));
	this->setStatic(false);
}
void Node::setRotationAnimation(const Animation<glm::quat>& animation) {
	this->rotationAnimation = std::make_unique<Animation<glm::quat>>(animation);
	this->setStatic(false);
}
void Node::setRotationAnimation(Animation<glm::quat>&& animation) {
	this->rotationAnimation = std::make_unique<Animation<glm::quat>>(std::move(animation));
	this->setStatic(false);
}
void Node::setScaleAnimation(const Animation<glm::vec3>& animation) {
	this->scaleAnimation = std::make_unique<Animation<glm::vec3>>(animation);
	this->setStatic(false);
}
void Node::setScaleAnimation(Animation<glm::vec3>&& animation) {
	this->scaleAnimation = std::make_unique<Animation<glm::vec3>>(std::move(animation));
	this->setStatic(false);
}

void Node::setStatic(bool isStatic) {
	if (isStatic) {
		if (!this->translationAnimation && !this->rotationAnimation && !this->scaleAnimation)
			this->_isStatic = true;
	}
	else this->_isStatic = false;

	for (auto& child : this->children) {
		child->setStatic(this->_isStatic);
	}
}