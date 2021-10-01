
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "VulkanRenderer.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include <tiny_gltf.h>

#include <glm/trigonometric.hpp>

#include "Mesh.h"

int main(size_t argc, char** argv) {
	tinygltf::TinyGLTF gltfLoader;
	tinygltf::Model gltfModel;
	gltfLoader.LoadASCIIFromFile(&gltfModel, nullptr, nullptr, "C:\\Users\\samli\\source\\glTF-Sample-Models-master\\2.0\\Duck\\glTF\\Duck.gltf");

	Mesh loadedMesh;

	for (auto& node : gltfModel.nodes) {
		if (node.mesh > -1) {
			const auto& mesh = gltfModel.meshes[node.mesh];

			const auto& posAccessor = gltfModel.accessors[mesh.primitives[0].attributes.at("POSITION")];
			const auto& posBufferView = gltfModel.bufferViews[posAccessor.bufferView];
			const auto& posBuffer = gltfModel.buffers[posBufferView.buffer];
			const byte* posData = &posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset];

			const auto& normalAccessor = gltfModel.accessors[mesh.primitives[0].attributes.at("NORMAL")];
			const auto& normalBufferView = gltfModel.bufferViews[normalAccessor.bufferView];
			const auto& normalBuffer = gltfModel.buffers[normalBufferView.buffer];
			const byte* normalData = &normalBuffer.data[normalBufferView.byteOffset + normalAccessor.byteOffset];

			const auto& uvAccessor = gltfModel.accessors[mesh.primitives[0].attributes.at("TEXCOORD_0")];
			const auto& uvBufferView = gltfModel.bufferViews[uvAccessor.bufferView];
			const auto& uvBuffer = gltfModel.buffers[uvBufferView.buffer];
			const byte* uvData = &uvBuffer.data[uvBufferView.byteOffset + uvAccessor.byteOffset];

			loadedMesh.vertices.reserve(posAccessor.count);
			for (size_t i = 0; i < posAccessor.count; i++) {
				Vertex v{};
				const float* p;
				p = reinterpret_cast<const float*>(&posData[i * posBufferView.byteStride]);
				v.pos = { p[0], p[1], p[2] };
				p = reinterpret_cast<const float*>(&normalData[i * normalBufferView.byteStride]);
				v.normal = { p[0], p[1], p[2] };
				p = reinterpret_cast<const float*>(&uvData[i * uvBufferView.byteStride]);
				v.uv = { p[0], p[1] };
				loadedMesh.vertices.push_back(v);
			}

			const auto& indicesAccessor = gltfModel.accessors[mesh.primitives[0].indices];
			const auto& indicesBufferView = gltfModel.bufferViews[indicesAccessor.bufferView];
			const auto& indicesBuffer = gltfModel.buffers[indicesBufferView.buffer];

			loadedMesh.triangleIndices.reserve(indicesAccessor.count);
			for (size_t i = 0; i < indicesAccessor.count; i++) {
				uint16_t index;
				const byte* p = &indicesBuffer.data[indicesBufferView.byteOffset + indicesAccessor.byteOffset];
				switch (indicesAccessor.componentType) {
				case TINYGLTF_COMPONENT_TYPE_BYTE:
					index = reinterpret_cast<const int8_t*>(p)[i];
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					index = reinterpret_cast<const uint8_t*>(p)[i];
					break;
				case TINYGLTF_COMPONENT_TYPE_SHORT:
					index = reinterpret_cast<const int16_t*>(p)[i];
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					index = reinterpret_cast<const uint16_t*>(p)[i];
					break;
				case TINYGLTF_COMPONENT_TYPE_INT:
					index = reinterpret_cast<const int32_t*>(p)[i];
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					index = reinterpret_cast<const uint32_t*>(p)[i];
					break;
				}
				loadedMesh.triangleIndices.push_back(index);
			}
			break;
		}
	}

	VulkanRenderer renderer;
	std::vector<PointLight> lights{};
	for (int i = 0; i < 16; i++) {
		const float angle = glm::radians(360.0f) / 16 * i;
		const float radius = 10.0f;
		lights.push_back(PointLight{ {radius * glm::sin(angle), 10.0f, radius * glm::cos(angle)}, {100.0f, 100.0f, 100.0f} });
	}
	renderer.setLights(lights);
	renderer.setTexture(gltfModel.images[0].image, gltfModel.images[0].width, gltfModel.images[0].height);
	renderer.setMesh(loadedMesh);
	renderer.run();
}