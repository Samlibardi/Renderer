#define NOMINMAX

#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "VulkanRenderer.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#include <tiny_gltf.h>

#include <glm/trigonometric.hpp>
#include <glm/geometric.hpp>

#include "Mesh.h"

template<uint32_t N> std::vector<glm::vec<N, float>> readAttribute(const tinygltf::Model& model, const tinygltf::Primitive& primitive, const char* attributeName) {
	const auto& accessor = model.accessors[primitive.attributes.at(attributeName)];
	const auto& bufferView = model.bufferViews[accessor.bufferView];
	const auto& buffer = model.buffers[bufferView.buffer];
	const byte* data = &buffer.data[bufferView.byteOffset + accessor.byteOffset];

	std::vector<glm::vec<N, float>> values;
	for (size_t i = 0; i < accessor.count; i++) {
		const glm::vec<N, float>& p = *reinterpret_cast<const glm::vec<N, float>*>(&data[i * (bufferView.byteStride != 0 ? bufferView.byteStride : sizeof(float) * N)]);
		values.push_back(p);
	}
	return values;
}

TextureInfo loadTexture(const char* path) {
	int w, h, n;
	byte* d = stbi_load(path, &w, &h, &n, 0);
	size_t len = static_cast<size_t>(w) * h * n;

	std::vector<byte> data;
	data.resize(len);
	memcpy(data.data(), d, len);

	stbi_image_free(d);

	return TextureInfo{ data, static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
}

int main(size_t argc, char** argv) {
	tinygltf::TinyGLTF gltfLoader;
	tinygltf::Model gltfModel;
	gltfLoader.LoadASCIIFromFile(&gltfModel, nullptr, nullptr, "C:\\Users\\samli\\source\\glTF-Sample-Models-master\\2.0\\EnvironmentTest\\glTF\\EnvironmentTest.gltf");

	Mesh loadedMesh;
	bool meshHasTangents = false;

	for (auto& node : gltfModel.nodes) {
		if (node.mesh > -1) {
			const auto& mesh = gltfModel.meshes[node.mesh];
			const auto& primitive = mesh.primitives[0];

			std::vector<glm::vec3> positions = readAttribute<3>(gltfModel, primitive, "POSITION");
			loadedMesh.vertices.reserve(positions.size());
			for (glm::vec3& p : positions)
				loadedMesh.vertices.push_back(Vertex{ p });

			if (primitive.attributes.count("NORMAL")) {
				std::vector<glm::vec3> normals = readAttribute<3>(gltfModel, primitive, "NORMAL");
				for (size_t i = 0; i < loadedMesh.vertices.size(); i++)
					loadedMesh.vertices[i].normal = normals[i];
			}

			if (primitive.attributes.count("TEXCOORD_0")) {
				std::vector<glm::vec2> uvs = readAttribute<2>(gltfModel, primitive, "TEXCOORD_0");
				for (size_t i = 0; i < loadedMesh.vertices.size(); i++)
					loadedMesh.vertices[i].uv = uvs[i];
			}

			if (primitive.attributes.count("TANGENT")) {
				meshHasTangents = true;
				std::vector<glm::vec4> tangents = readAttribute<4>(gltfModel, primitive, "TANGENT");
				for (size_t i = 0; i < loadedMesh.vertices.size(); i++) {
					loadedMesh.vertices[i].tangent = glm::vec3{ tangents[i].x, tangents[i].y, tangents[i].z };
					loadedMesh.vertices[i].bitangent = glm::cross(loadedMesh.vertices[i].normal, loadedMesh.vertices[i].tangent) * tangents[i].w;
				}
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
	std::vector<PointLight> lights{ {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}} };
	for (int i = 0; i < 0; i++) {
		const float angle = glm::radians(360.0f) / 16 * i;
		const float radius = 10.0f;
		lights.push_back(PointLight{ {radius * glm::sin(angle), 10.0f, radius * glm::cos(angle)}, {100.0f, 100.0f, 100.0f} });
	}
	renderer.setLights(lights);
	
	
	TextureInfo albedoInfo{};
	TextureInfo normalInfo{ {0x80, 0x80, 0xff, 0x00}, 1, 1 };
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

	if(!meshHasTangents)
		loadedMesh.calculateTangents();
	renderer.setMesh(loadedMesh);

	std::array<TextureInfo, 6> cubeFaces = {
		loadTexture("./environment/px.png"),
		loadTexture("./environment/nx.png"),
		loadTexture("./environment/py.png"),
		loadTexture("./environment/ny.png"),
		loadTexture("./environment/pz.png"),
		loadTexture("./environment/nz.png"),
	};
	renderer.setEnvironmentMap(cubeFaces);

	renderer.run();
}