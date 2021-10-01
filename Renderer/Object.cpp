#include "Object.h"

#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

void Object::setTranslation(glm::vec3 translation) {
    this->translation = translation;
    this->transformDirty = true;
}

void Object::setRotation(glm::vec3 eulerRotation) {
    this->rotation = glm::quat(eulerRotation);
    this->transformDirty = true;
}

void Object::setRotation(glm::qua<float> quaternionRotation) {
    this->rotation = quaternionRotation;
    this->transformDirty = true;
}

void Object::setScale(glm::vec3 scale) {
    this->translation = translation;
    this->transformDirty = true;
}

glm::mat4 Object::getTransformMatrix()
{
    if (!this->transformDirty) {
        this->transform = glm::translate(this->translation) * glm::toMat4(this->rotation) * glm::scale(this->scale);
    }
    this->transformDirty = false;
    return this->transform;
}

