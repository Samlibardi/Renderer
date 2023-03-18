#pragma once

#include <vector>
#include <map>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>

#include "Buffer.h"
#include "Node.h"

enum class AttributeValueType {
	eInt8,
	eInt16,
	eInt32,
	eInt64,
	eUint8,
	eUint16,
	eUint32,
	eUint64,
	eHalf,
	eFloat,
	eDouble
};


constexpr size_t sizeFromAttributeValueType(const AttributeValueType valueType) {
	switch (valueType) {
	case AttributeValueType::eInt8:
	case AttributeValueType::eUint8:
		return 1;
	case AttributeValueType::eInt16:
	case AttributeValueType::eUint16:
	case AttributeValueType::eHalf:
		return 2;
	case AttributeValueType::eInt32:
	case AttributeValueType::eUint32:
	case AttributeValueType::eFloat:
		return 4;
	case AttributeValueType::eInt64:
	case AttributeValueType::eUint64:
	case AttributeValueType::eDouble:
		return 8;
	}
}

enum class AttributeContainerType {
	eScalar,
	eVec2,
	eVec3,
	eVec4,
};

enum class MeshPrimitiveMode {
	ePoints,
	eLines,
	eLineLoop,
	eLineStrip,
	eTriangles,
	eTriangleStrip,
	eTriangleFan
};

struct VertexAttributeDescription {
	std::string attributeName;
	Buffer buffer;
	size_t offset;
	int32_t stride;
	size_t count;
	AttributeContainerType containerType;
	AttributeValueType valueType;
};

struct IndexBufferDescription {
	Buffer buffer;
	size_t offset;
	int32_t stride;
	size_t count;
	AttributeValueType indexType;
};

class MeshPrimitive {
	std::vector<VertexAttributeDescription> m_vertexBufferDescription{};
	bool m_isIndexed = false;
	IndexBufferDescription m_indexBufferDescription{};
	glm::vec<3, double> m_bbMin, m_bbMax;
	MeshPrimitiveMode m_mode = MeshPrimitiveMode::eTriangles;

public:
	MeshPrimitive() {};
	MeshPrimitive(std::vector<VertexAttributeDescription> _vertexBufferDescription, glm::vec3 _bbMin, glm::vec3 _bbMax, MeshPrimitiveMode _mode) : 
		m_vertexBufferDescription(_vertexBufferDescription),
		m_bbMin(_bbMin),
		m_bbMax(_bbMax),
		m_mode(_mode)
	{};
	MeshPrimitive(std::vector<VertexAttributeDescription> _vertexBufferDescription, IndexBufferDescription _indexBufferDescription, glm::vec3 _bbMin, glm::vec3 _bbMax, MeshPrimitiveMode _mode) :
		m_vertexBufferDescription(_vertexBufferDescription),
		m_isIndexed(true),
		m_indexBufferDescription(_indexBufferDescription),
		m_bbMin(_bbMin),
		m_bbMax(_bbMax),
		m_mode(_mode)
	{};

	const std::vector<VertexAttributeDescription>& vertexBufferDescription() { return this->m_vertexBufferDescription; };
	const IndexBufferDescription& indexBufferDescription() { return this->m_indexBufferDescription; };
	const bool isIndexed() { return this->m_isIndexed; };
};

class Mesh {
	std::vector<MeshPrimitive> primitives;

	Mesh(std::vector<MeshPrimitive> _primitives, std::shared_ptr<Node> _node) : primitives(_primitives), node(_node) {};
};
