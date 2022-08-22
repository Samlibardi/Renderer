#pragma once

#include <vector>
#include <map>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "Buffer.h"

enum class ValueType {
	eFloat,
	eDouble,
	eInt8,
	eInt16,
	eInt32,
	eInt64
};

enum class AttributeType {
	eScalar,
	eVec2,
	eVec3,
	eVec4,
};

struct VertexAttributeDescription {
	std::string attributeName;
	Buffer buffer;
	size_t offset;
	size_t stride;
	AttributeType componentType;
	ValueType valueType;
};

struct IndexBufferDescription {
	Buffer buffer;
	size_t offset;
	size_t stride;
	ValueType indexType;
};

class Mesh {
	size_t id;
	std::vector<VertexAttributeDescription> vertexBufferDescription;
	bool isIndexed;
	IndexBufferDescription indexBufferDescription;
	
public:
	bool operator==(const Mesh& rhs) { return this->id == rhs.id; }


};

