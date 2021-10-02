#define NOMINMAX

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
	gltfLoader.LoadASCIIFromFile(&gltfModel, nullptr, nullptr, "C:\\Users\\samli\\source\\glTF-Sample-Models-master\\2.0\\SciFiHelmet\\glTF\\SciFiHelmet.gltf");

	Mesh loadedMesh;

	for (auto& node : gltfModel.nodes) {
		if (node.mesh > -1) {
			const auto& mesh = gltfModel.meshes[node.mesh];
			const auto& primitive = mesh.primitives[0];

			const auto& posAccessor = gltfModel.accessors[primitive.attributes.at("POSITION")];
			const auto& posBufferView = gltfModel.bufferViews[posAccessor.bufferView];
			const auto& posBuffer = gltfModel.buffers[posBufferView.buffer];
			const byte* posData = &posBuffer.data[posBufferView.byteOffset + posAccessor.byteOffset];

			const auto& normalAccessor = gltfModel.accessors[primitive.attributes.at("NORMAL")];
			const auto& normalBufferView = gltfModel.bufferViews[normalAccessor.bufferView];
			const auto& normalBuffer = gltfModel.buffers[normalBufferView.buffer];
			const byte* normalData = &normalBuffer.data[normalBufferView.byteOffset + normalAccessor.byteOffset];

			const auto& uvAccessor = gltfModel.accessors[primitive.attributes.at("TEXCOORD_0")];
			const auto& uvBufferView = gltfModel.bufferViews[uvAccessor.bufferView];
			const auto& uvBuffer = gltfModel.buffers[uvBufferView.buffer];
			const byte* uvData = &uvBuffer.data[uvBufferView.byteOffset + uvAccessor.byteOffset];

			const auto& tangentAccessor = gltfModel.accessors[primitive.attributes.at("TANGENT")];
			const auto& tangentBufferView = gltfModel.bufferViews[tangentAccessor.bufferView];
			const auto& tangentBuffer = gltfModel.buffers[tangentBufferView.buffer];
			const byte* tangentData = &tangentBuffer.data[tangentBufferView.byteOffset + tangentAccessor.byteOffset];

			loadedMesh.vertices.reserve(posAccessor.count);
			for (size_t i = 0; i < posAccessor.count; i++) {
				Vertex v{};
				const float* p;
				p = reinterpret_cast<const float*>(&posData[i * (posBufferView.byteStride != 0 ? posBufferView.byteStride : sizeof(float) * 3)]);
				v.pos = { p[0], p[1], p[2] };
				p = reinterpret_cast<const float*>(&normalData[i * (normalBufferView.byteStride != 0 ? normalBufferView.byteStride : sizeof(float) * 3)]);
				v.normal = { p[0], p[1], p[2] };
				p = reinterpret_cast<const float*>(&uvData[i * (uvBufferView.byteStride != 0 ? uvBufferView.byteStride : sizeof(float) * 2)]);
				v.uv = { p[0], p[1] };
				p = reinterpret_cast<const float*>(&tangentData[i * (tangentBufferView.byteStride != 0 ? tangentBufferView.byteStride : sizeof(float) * 3)]);
				v.tangent = { p[0], p[1], p[2] };
				loadedMesh.vertices.push_back(v);
			}

			if (primitive.indices > -1) {
				loadedMesh.isIndexed = true;

				const auto& indicesAccessor = gltfModel.accessors[mesh.primitives[0].indices];
				const auto& indicesBufferView = gltfModel.bufferViews[indicesAccessor.bufferView];
				const auto& indicesBuffer = gltfModel.buffers[indicesBufferView.buffer];

				loadedMesh.triangleIndices.reserve(indicesAccessor.count);
				for (size_t i = 0; i < indicesAccessor.count; i++) {
					uint32_t index;
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
			else {
				loadedMesh.isIndexed = false;
			}
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
	
	
	TextureInfo albedoInfo{};
	TextureInfo normalInfo{};
	TextureInfo metallicRoughInfo{};
	TextureInfo aoInfo{};
	TextureInfo emissiveInfo{};

	tinygltf::Material& material = gltfModel.materials[0];
	if (material.pbrMetallicRoughness.baseColorTexture.index != -1) {
		tinygltf::Image& image = gltfModel.images[gltfModel.textures[material.pbrMetallicRoughness.baseColorTexture.index].source];
		albedoInfo = TextureInfo{ image.image, static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height) };
	}
	if (material.pbrMetallicRoughness.metallicRoughnessTexture.index != -1) {
		tinygltf::Image& image = gltfModel.images[gltfModel.textures[material.pbrMetallicRoughness.metallicRoughnessTexture.index].source];
		metallicRoughInfo = TextureInfo{ image.image, static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height) };
	}
	if (material.normalTexture.index != -1) {
		tinygltf::Image& image = gltfModel.images[gltfModel.textures[material.normalTexture.index].source];
		normalInfo = TextureInfo{ image.image, static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height) };
	}
	if (material.emissiveTexture.index != -1) {
		tinygltf::Image& image = gltfModel.images[gltfModel.textures[material.emissiveTexture.index].source];
		emissiveInfo = TextureInfo{ image.image, static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height) };
	}
	if (material.occlusionTexture.index != -1) {
		tinygltf::Image& image = gltfModel.images[gltfModel.textures[material.occlusionTexture.index].source];
		aoInfo = TextureInfo{ image.image, static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height) };
	}
	
	renderer.setTextures(albedoInfo, normalInfo, metallicRoughInfo, aoInfo, emissiveInfo);

	auto& bcf = material.pbrMetallicRoughness.baseColorFactor;
	auto& ef = material.emissiveFactor;
	renderer.setPBRParameters(PBRInfo{ glm::vec4{bcf[0], bcf[1], bcf[2], bcf[3]}, glm::vec4{ef[0], ef[1], ef[2], 0.0f}, static_cast<float>(material.normalTexture.scale), static_cast<float>(material.pbrMetallicRoughness.metallicFactor), static_cast<float>(material.pbrMetallicRoughness.roughnessFactor), static_cast<float>(material.occlusionTexture.strength) });

	//loadedMesh.calculateTangents();
	renderer.setMesh(loadedMesh);
	renderer.run();
}