
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "VulkanRenderer.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include <tiny_gltf.h>

#include "Mesh.h"

int main(size_t argc, char** argv) {
	tinygltf::TinyGLTF gltfLoader;
	tinygltf::Model gltfModel;
	gltfLoader.LoadASCIIFromFile(&gltfModel, nullptr, nullptr, "C:\\Users\\samli\\source\\glTF-Sample-Models-master\\2.0\\Lantern\\glTF\\Lantern.gltf");

	Mesh cube;
	cube.vertices = {
			{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 1.0f}},
			{{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 1.0f}},
			{{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 1.0f}},
			{{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.5f, 1.0f}},

			{{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.5f, 0.0f}},
			{{1.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.5f, 0.0f}},
			{{1.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.5f, 0.0f}},
			{{0.0f, 1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {0.5f, 0.5f, 0.0f}},

			{{1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.5f}},
			{{1.0f, 1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.5f, 0.5f}},
			{{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.5f,  0.5f}},
			{{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.5f,  0.5f}},

			{{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.5f}},
			{{0.0f, 1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.5f}},
			{{0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.5f}},
			{{0.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.5f}},

			{{1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.5f}},
			{{1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.5f}},
			{{0.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.5f}},
			{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.5f}},

			{{1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.0f, 0.5f}},
			{{1.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.0f, 0.5f}},
			{{0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.0f, 0.5f}},
			{{0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.0f, 0.5f}},
	};
	cube.triangleIndices = {
		0, 1, 2,
		2, 3, 0,
		4, 7, 6,
		6, 5, 4,
		8, 9, 10,
		10, 11, 8,
		12, 15, 14,
		14, 13, 12,
		18, 16, 17,
		18, 19, 16,
		22, 21, 20,
		23, 22, 20,
	};


	Mesh loadedMesh;

	for (auto& node : gltfModel.nodes) {
		if (node.mesh > -1) {
			const auto& mesh = gltfModel.meshes[node.mesh];

			const auto& posAccessor = gltfModel.accessors[mesh.primitives[0].attributes.at("POSITION")];
			const auto& posBufferView = gltfModel.bufferViews[posAccessor.bufferView];
			const auto& posBuffer = gltfModel.buffers[posBufferView.buffer];

			const auto& normalAccessor = gltfModel.accessors[mesh.primitives[0].attributes.at("NORMAL")];
			const auto& normalBufferView = gltfModel.bufferViews[normalAccessor.bufferView];
			const auto& normalBuffer = gltfModel.buffers[normalBufferView.buffer];

			const auto& uvAccessor = gltfModel.accessors[mesh.primitives[0].attributes.at("TEXCOORD_0")];
			const auto& uvBufferView = gltfModel.bufferViews[uvAccessor.bufferView];
			const auto& uvBuffer = gltfModel.buffers[uvBufferView.buffer];

			loadedMesh.vertices.reserve(posAccessor.count);
			for (size_t i = 0; i < posAccessor.count; i++) {
				Vertex v{};
				const float* p = reinterpret_cast<const float*>(&posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset]);
				v.pos = { p[i * 3 + 0], p[i * 3 + 1], p[i * 3 + 2] };
				const float* n = reinterpret_cast<const float*>(&normalBuffer.data[normalBufferView.byteOffset + normalAccessor.byteOffset]);
				v.normal = { n[i * 3 + 0], n[i * 3 + 1], n[i * 3 + 2] };
				const float* u = reinterpret_cast<const float*>(&uvBuffer.data[uvBufferView.byteOffset + uvAccessor.byteOffset]);
				v.uv = { u[i * 2 + 0], u[i * 2 + 1] };
				loadedMesh.vertices.push_back(v);
			}

			const auto& indicesAccessor = gltfModel.accessors[mesh.primitives[0].indices];
			const auto& indicesBufferView = gltfModel.bufferViews[indicesAccessor.bufferView];
			const auto& indicesBuffer = gltfModel.buffers[indicesBufferView.buffer];

			loadedMesh.triangleIndices.reserve(indicesAccessor.count);
			for (size_t i = 0; i < indicesAccessor.count; i++) {
				uint16_t index;
				switch (indicesAccessor.componentType) {
				case TINYGLTF_COMPONENT_TYPE_BYTE:
					index = *(reinterpret_cast<const int8_t*>(&indicesBuffer.data[indicesBufferView.byteOffset + indicesAccessor.byteOffset]) + i);
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					index = *(reinterpret_cast<const uint8_t*>(&indicesBuffer.data[indicesBufferView.byteOffset + indicesAccessor.byteOffset]) + i);
					break;
				case TINYGLTF_COMPONENT_TYPE_SHORT:
					index = *(reinterpret_cast<const int16_t*>(&indicesBuffer.data[indicesBufferView.byteOffset + indicesAccessor.byteOffset]) + i);
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					index = *(reinterpret_cast<const uint16_t*>(&indicesBuffer.data[indicesBufferView.byteOffset + indicesAccessor.byteOffset]) + i);
					break;
				case TINYGLTF_COMPONENT_TYPE_INT:
					index = *(reinterpret_cast<const int32_t*>(&indicesBuffer.data[indicesBufferView.byteOffset + indicesAccessor.byteOffset]) + i);
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					index = *(reinterpret_cast<const uint32_t*>(&indicesBuffer.data[indicesBufferView.byteOffset + indicesAccessor.byteOffset]) + i);
					break;
				}
				loadedMesh.triangleIndices.push_back(index);
			}
			break;
		}
	}

	VulkanRenderer renderer;
	renderer.setTexture(gltfModel.images[0].image, gltfModel.images[0].width, gltfModel.images[0].height);
	renderer.setMesh(loadedMesh);
	renderer.run();
}