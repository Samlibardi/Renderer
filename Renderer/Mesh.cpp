#include "Mesh.h"

#include <glm/gtx/transform.hpp>
//
//void MeshPrimitive::calculateTangents()
//{
//	for (Vertex& v : this->vertices) {
//		v.tangent = glm::vec3(0.0f);
//	}
//
//	if (isIndexed) {
//		for (size_t i = 0; i < this->indices.size() - 2; i += 3) {
//			Vertex& v0 = this->vertices[this->indices[i]];
//			Vertex& v1 = this->vertices[this->indices[i+1]];
//			Vertex& v2 = this->vertices[this->indices[i+2]];
//
//			glm::vec3& p0 = v0.pos;
//			glm::vec3& p1 = v1.pos;
//			glm::vec3& p2 = v2.pos;
//
//			glm::vec2& uv0 = v0.uv;
//			glm::vec2& uv1 = v1.uv;
//			glm::vec2& uv2 = v2.uv;
//
//			float x1 = p1.x - p0.x;
//			float x2 = p2.x - p0.x;
//			float y1 = p1.y - p0.y;
//			float y2 = p2.y - p0.y;
//			float z1 = p1.z - p0.z;
//			float z2 = p2.z - p0.z;
//
//			float s1 = uv1.x - uv0.x;
//			float s2 = uv2.x - uv0.x;
//			float t1 = uv1.y - uv0.y;
//			float t2 = uv2.y - uv0.y;
//
//			float f = 1.0f / (s1 * t2 - s2 * t1);
//			glm::vec3 sdir{ 
//				(t2 * x1 - t1 * x2) * f,
//				(t2 * y1 - t1 * y2) * f,
//				(t2 * z1 - t1 * z2) * f 
//			};
//
//			v0.tangent = sdir;
//			v1.tangent = sdir;
//			v2.tangent = sdir;
//
//			v0.bitangent = glm::cross(v0.normal, v0.tangent);
//			v1.bitangent = glm::cross(v1.normal, v1.tangent);
//			v2.bitangent = glm::cross(v2.normal, v2.tangent);
//		}
//	}
//	else {
//		for (size_t i = 0; i < this->vertices.size() - 2; i++) {
//			Vertex& v0 = this->vertices[i];
//			Vertex& v1 = this->vertices[i + 1];
//			Vertex& v2 = this->vertices[i + 2];
//
//			glm::vec3 e0 = v1.pos - v0.pos;
//			glm::vec3 e1 = v2.pos - v0.pos;
//
//			glm::vec2 duv0 = v1.uv - v0.uv;
//			glm::vec2 duv1 = v2.uv - v0.uv;
//
//			float f = 1.0f / (duv0.x * duv1.y - duv1.x * duv0.y);
//
//			glm::vec3 t;
//			t.x = f * (duv1.y * e0.x - duv0.y * e1.x);
//			t.y = f * (duv1.y * e0.y - duv0.y * e1.y);
//			t.z = f * (duv1.y * e0.z - duv0.y * e1.z);
//
//			v0.tangent = glm::normalize(t);
//		}
//	}
//}
//
//void MeshPrimitive::calculateBarycenter() {
//	this->_barycenter = glm::vec3(0.0f);
//	for (const auto& v : this->vertices) {
//		this->_barycenter += v.pos;
//	}
//	this->_barycenter /= this->vertices.size();
//
//	glm::vec4 bch = this->node->modelMatrix() * glm::vec4(this->_barycenter, 1.0f);
//	this->_barycenter = glm::vec3(bch)/bch.w;
//}
//
//void MeshPrimitive::calculateBoundingBox() {
//	this->boundingBox = { glm::vec3(INFINITY), glm::vec3(-INFINITY) } ;
//	for (const auto& v : this->vertices) {
//		if (v.pos.x < this->boundingBox.first.x) this->boundingBox.first.x = v.pos.x;
//		if (v.pos.y < this->boundingBox.first.y) this->boundingBox.first.y = v.pos.y;
//		if (v.pos.z < this->boundingBox.first.z) this->boundingBox.first.z = v.pos.z;
//
//		if (v.pos.x > this->boundingBox.second.x) this->boundingBox.second.x = v.pos.x;
//		if (v.pos.y > this->boundingBox.second.y) this->boundingBox.second.y = v.pos.y;
//		if (v.pos.z > this->boundingBox.second.z) this->boundingBox.second.z = v.pos.z;
//	}
//}
//
//MeshPrimitive MeshPrimitive::boundingBoxAsMesh() const {
//	MeshPrimitive m{};
//	m.vertices = {
//		Vertex{glm::vec3{this->boundingBox.first.x, this->boundingBox.first.y, this->boundingBox.first.z}},
//		Vertex{glm::vec3{this->boundingBox.first.x, this->boundingBox.first.y, this->boundingBox.second.z}},
//		Vertex{glm::vec3{this->boundingBox.first.x, this->boundingBox.second.y, this->boundingBox.second.z}},
//		Vertex{glm::vec3{this->boundingBox.first.x, this->boundingBox.second.y, this->boundingBox.first.z}},
//		Vertex{glm::vec3{this->boundingBox.second.x, this->boundingBox.second.y, this->boundingBox.first.z}},
//		Vertex{glm::vec3{this->boundingBox.second.x, this->boundingBox.second.y, this->boundingBox.second.z}},
//		Vertex{glm::vec3{this->boundingBox.second.x, this->boundingBox.first.y, this->boundingBox.second.z}},
//		Vertex{glm::vec3{this->boundingBox.second.x, this->boundingBox.first.y, this->boundingBox.first.z}}
//	};
//	for (auto& vertex : m.vertices) {
//		vertex.normal = vertex.pos;
//	};
//
//	m.indices = {
//		0, 1, 0,
//		0, 3, 0,
//		0, 7, 0,
//		1, 2, 1,
//		1, 6, 1,
//		2, 3, 2,
//		2, 5, 2,
//		3, 4, 3,
//		4, 5, 4,
//		4, 7, 4,
//		5, 6, 5,
//		6, 7, 6,
//	};
//	m.isIndexed = true;
//	m.node = this->node;
//	m.calculateBarycenter();
//
//	m.boundingBox = this->boundingBox;
//	
//	return m;
//}
