#include "Mesh.h"

#include <glm/geometric.hpp>
#include <glm/gtx/transform.hpp>

void Mesh::calculateTangents()
{
	for (Vertex& v : this->vertices) {
		v.tangent = glm::vec3(0.0f);
	}

	if (isIndexed) {
		for (size_t i = 0; i < this->indices.size() - 2; i += 3) {
			Vertex& v0 = this->vertices[this->indices[i]];
			Vertex& v1 = this->vertices[this->indices[i+1]];
			Vertex& v2 = this->vertices[this->indices[i+2]];

			glm::vec3& p0 = v0.pos;
			glm::vec3& p1 = v1.pos;
			glm::vec3& p2 = v2.pos;

			glm::vec2& uv0 = v0.uv;
			glm::vec2& uv1 = v1.uv;
			glm::vec2& uv2 = v2.uv;

			float x1 = p1.x - p0.x;
			float x2 = p2.x - p0.x;
			float y1 = p1.y - p0.y;
			float y2 = p2.y - p0.y;
			float z1 = p1.z - p0.z;
			float z2 = p2.z - p0.z;

			float s1 = uv1.x - uv0.x;
			float s2 = uv2.x - uv0.x;
			float t1 = uv1.y - uv0.y;
			float t2 = uv2.y - uv0.y;

			float f = 1.0f / (s1 * t2 - s2 * t1);
			glm::vec3 sdir{ 
				(t2 * x1 - t1 * x2) * f,
				(t2 * y1 - t1 * y2) * f,
				(t2 * z1 - t1 * z2) * f 
			};

			v0.tangent = sdir;
			v1.tangent = sdir;
			v2.tangent = sdir;

			v0.bitangent = glm::cross(v0.normal, v0.tangent);
			v1.bitangent = glm::cross(v1.normal, v1.tangent);
			v2.bitangent = glm::cross(v2.normal, v2.tangent);
		}
	}
	else {
		for (size_t i = 0; i < this->vertices.size() - 2; i++) {
			Vertex& v0 = this->vertices[i];
			Vertex& v1 = this->vertices[i + 1];
			Vertex& v2 = this->vertices[i + 2];

			glm::vec3 e0 = v1.pos - v0.pos;
			glm::vec3 e1 = v2.pos - v0.pos;

			glm::vec2 duv0 = v1.uv - v0.uv;
			glm::vec2 duv1 = v2.uv - v0.uv;

			float f = 1.0f / (duv0.x * duv1.y - duv1.x * duv0.y);

			glm::vec3 t;
			t.x = f * (duv1.y * e0.x - duv0.y * e1.x);
			t.y = f * (duv1.y * e0.y - duv0.y * e1.y);
			t.z = f * (duv1.y * e0.z - duv0.y * e1.z);

			v0.tangent = glm::normalize(t);
		}
	}
}

void Mesh::calculateBarycenter() {
	this->barycenter = glm::vec3(0.0f);
	for (const auto& v : this->vertices) {
		this->barycenter += v.pos;
	}
	this->barycenter /= this->vertices.size();

	this->barycenter = glm::vec3(glm::translate(this->translation) * glm::rotate(this->rotation.x, glm::vec3{ 0.0f, 0.0f, 1.0f }) * glm::rotate(this->rotation.y, glm::vec3{ 1.0f, 0.0f, 0.0f }) * glm::rotate(this->rotation.z, glm::vec3{ 0.0f, 1.0f, 0.0f }) * glm::scale(this->scale) * glm::vec4(this->barycenter, 1.0f));
}
