#define NOMINMAX

#include <memory>
#include <filesystem>
#include <iostream>
#include <set>

#include <glm/glm.hpp>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>

#include <vkfw/vkfw.hpp>

#include <imgui.h>
#include "bindings/imgui_impl_glfw.h"
#include "bindings/imgui_impl_vulkan.h"

#include "VulkanRenderer.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_MSC_SECURE_CRT
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include <tiny_gltf.h>

#include "Mesh.h"

typedef unsigned char byte;

std::unique_ptr<VulkanRenderer> renderer = nullptr;

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
	float* d = stbi_loadf(path, &w, &h, &n, 4);
	size_t len = sizeof(float) * w * h * 4;

	std::vector<byte> data;
	data.resize(len);
	memcpy(data.data(), d, len);

	stbi_image_free(d);

	return TextureInfo{ data, static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
}

std::vector<Mesh> makeMesh(tinygltf::Mesh gltfMesh, std::shared_ptr<Node> node, tinygltf::Model gltfModel, const std::string& gltfFilename) {
	auto path = std::filesystem::path(gltfFilename).remove_filename();

	std::vector<Mesh> loadedMeshes;
	for (auto& primitive : gltfMesh.primitives) {
		Mesh loadedMesh{};
		bool meshHasTangents = false;

		loadedMesh.node = node;

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

			const auto& indicesAccessor = gltfModel.accessors[primitive.indices];
			const auto& indicesBufferView = gltfModel.bufferViews[indicesAccessor.bufferView];
			const auto& indicesBuffer = gltfModel.buffers[indicesBufferView.buffer];

			loadedMesh.indices.reserve(indicesAccessor.count);
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
				loadedMesh.indices.push_back(index);
			}
		}
		else {
			loadedMesh.isIndexed = false;
		}

		if (primitive.material > -1) {
			tinygltf::Material& material = gltfModel.materials[primitive.material];
			if (material.pbrMetallicRoughness.baseColorTexture.index != -1) {
				loadedMesh.albedoTexture = Texture((path / gltfModel.images[gltfModel.textures[material.pbrMetallicRoughness.baseColorTexture.index].source].uri).string(), vk::Format::eR8G8B8A8Srgb);
			}

			if (material.pbrMetallicRoughness.metallicRoughnessTexture.index != -1) {
				loadedMesh.metalRoughnessTexture = Texture((path / gltfModel.images[gltfModel.textures[material.pbrMetallicRoughness.metallicRoughnessTexture.index].source].uri).string());
			}

			if (material.normalTexture.index != -1) {
				loadedMesh.normalTexture = Texture((path / gltfModel.images[gltfModel.textures[material.normalTexture.index].source].uri).string());
			}
			else {
				loadedMesh.normalTexture = Texture("./textures/clear_normal.png");
			}

			if (material.emissiveTexture.index != -1) {
				loadedMesh.emissiveTexture = Texture((path / gltfModel.images[gltfModel.textures[material.emissiveTexture.index].source].uri).string());
			}

			if (material.occlusionTexture.index != -1) {
				loadedMesh.aoTexture = Texture((path / gltfModel.images[gltfModel.textures[material.occlusionTexture.index].source].uri).string());
			}

			auto& bcf = material.pbrMetallicRoughness.baseColorFactor;
			auto& ef = material.emissiveFactor;
			loadedMesh.materialInfo = PBRInfo{ glm::vec4{bcf[0], bcf[1], bcf[2], bcf[3]}, glm::vec4{ef[0], ef[1], ef[2], 0.0f}, static_cast<float>(material.normalTexture.scale), static_cast<float>(material.pbrMetallicRoughness.metallicFactor), static_cast<float>(material.pbrMetallicRoughness.roughnessFactor), static_cast<float>(material.occlusionTexture.strength) };

			loadedMesh.alphaInfo.alphaMode = material.alphaMode == "MASK" ? AlphaMode::eMask : material.alphaMode == "BLEND" ? AlphaMode::eBlend : AlphaMode::eOpaque;
			loadedMesh.alphaInfo.alphaCutoff = material.alphaCutoff;
		}

		if (!meshHasTangents)
			loadedMesh.calculateTangents();

		loadedMesh.calculateBarycenter();
		loadedMesh.calculateBoundingBox();
		loadedMeshes.push_back(loadedMesh);
	}

	return loadedMeshes;
}

std::tuple<std::shared_ptr<Node>, std::vector<Mesh>> makeNode(const int nodeIndex, const tinygltf::Model& gltfModel, const std::string& gltfFilename) {

	auto& gltfNode = gltfModel.nodes[nodeIndex];

	std::vector<Mesh> meshes{};
	std::vector<std::shared_ptr<Node>> children{};
	for (auto& childIndex : gltfNode.children) {
		auto&& [childNode, childMeshes] = makeNode(childIndex, gltfModel, gltfFilename);
		children.push_back(childNode);
		for (auto& mesh : childMeshes) {
			meshes.emplace_back(mesh);
		}
	}

	std::shared_ptr<Node> node;

	if (!gltfNode.matrix.empty()) {
		auto& m = gltfNode.matrix;
		glm::mat4 modelMatrix{ m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9] ,m[10], m[11], m[12], m[13], m[14], m[15] };

		node = std::make_shared<Node>(modelMatrix, children);
	}
	else {
		glm::vec3 translation{ 0.0f }, rotation{ 0.0f }, scale{ 1.0f };

		if (!gltfNode.translation.empty()) {
			translation = glm::vec3{ gltfNode.translation[0], gltfNode.translation[1], gltfNode.translation[2] };
		}
		if (!gltfNode.rotation.empty()) {
			rotation = glm::vec3{ gltfNode.rotation[0], gltfNode.rotation[1], gltfNode.rotation[2] };
		}
		if (!gltfNode.scale.empty()) {
			scale = glm::vec3{ gltfNode.scale[0], gltfNode.scale[1], gltfNode.scale[2] };
		}

		node = std::make_shared<Node>(translation, rotation, scale, children);
	}


	if (gltfNode.mesh > -1) {
		for (auto& mesh : makeMesh(gltfModel.meshes[gltfNode.mesh], node, gltfModel, gltfFilename)) {
			meshes.push_back(mesh);
		}
	}

	for (auto& gltfAnimation : gltfModel.animations) {
		for (auto& channel : gltfAnimation.channels) {
			if (channel.target_node != nodeIndex)
				continue;

			auto& sampler = gltfAnimation.samplers[channel.sampler];

			AnimationInterpolationCurve interpolationCurve =
				sampler.interpolation == "CUBICSPLINE" ? AnimationInterpolationCurve::eCubicSpline :
				sampler.interpolation == "STEP" ? AnimationInterpolationCurve::eStep :
				AnimationInterpolationCurve::eLinear;
			
			const auto& keyframesAccessor = gltfModel.accessors[sampler.input];
			const auto& keyframesBufferView = gltfModel.bufferViews[keyframesAccessor.bufferView];
			const auto& keyframesBuffer = gltfModel.buffers[keyframesBufferView.buffer];
			const byte* keyframesBufferData = &keyframesBuffer.data[keyframesBufferView.byteOffset + keyframesAccessor.byteOffset];

			std::vector<float> keyframes;
			for (size_t i = 0; i < keyframesAccessor.count; i++) {
				auto& p = *reinterpret_cast<const float*>(&keyframesBufferData[i * (keyframesBufferView.byteStride != 0 ? keyframesBufferView.byteStride : sizeof(float))]);
				keyframes.push_back(p);
			}

			if (channel.target_path == "translation" || channel.target_path == "scale") {
				const auto& valuesAccessor = gltfModel.accessors[sampler.output];
				const auto& valuesBufferView = gltfModel.bufferViews[valuesAccessor.bufferView];
				const auto& valuesBuffer = gltfModel.buffers[valuesBufferView.buffer];
				const byte* valuesBufferData = &valuesBuffer.data[valuesBufferView.byteOffset + valuesAccessor.byteOffset];

				std::vector<glm::vec3> values;
				for (size_t i = 0; i < keyframesAccessor.count; i++) {
					auto& p = *reinterpret_cast<const glm::vec3*>(&valuesBufferData[i * (valuesBufferView.byteStride != 0 ? valuesBufferView.byteStride : sizeof(glm::vec3))]);
					values.push_back(p);
				}

				if (channel.target_path == "translation")
					node->setTranslationAnimation(Animation<glm::vec3>{keyframes, values, interpolationCurve, AnimationRepeatMode::eMirror});
				else if(channel.target_path == "scale")
					node->setScaleAnimation(Animation<glm::vec3>{keyframes, values, interpolationCurve, AnimationRepeatMode::eMirror});
			}
			else if (channel.target_path == "rotation") {
				const auto& valuesAccessor = gltfModel.accessors[sampler.output];
				const auto& valuesBufferView = gltfModel.bufferViews[valuesAccessor.bufferView];
				const auto& valuesBuffer = gltfModel.buffers[valuesBufferView.buffer];
				const byte* valuesBufferData = &valuesBuffer.data[valuesBufferView.byteOffset + valuesAccessor.byteOffset];

				std::vector<glm::quat> values;
				for (size_t i = 0; i < keyframesAccessor.count; i++) {
					auto& p = *reinterpret_cast<const glm::quat*>(&valuesBufferData[i * (valuesBufferView.byteStride != 0 ? valuesBufferView.byteStride : sizeof(glm::quat))]);
					values.push_back(p);
				}

				node->setRotationAnimation(Animation<glm::quat>{keyframes, values, interpolationCurve, AnimationRepeatMode::eMirror});
			}
		}
	}

	return { node, meshes };
}

void openGltf(const std::string& filename) {
	tinygltf::TinyGLTF gltfLoader;
	tinygltf::Model gltfModel;
	gltfLoader.LoadASCIIFromFile(&gltfModel, nullptr, nullptr, filename);

	std::vector<Mesh> loadedMeshes{};
	std::vector<std::shared_ptr<Node>> rootNodes;
	tinygltf::Scene& scene = gltfModel.scenes[gltfModel.defaultScene];
	
	for (auto& gltfNode : scene.nodes) {
		auto [childNode, childMeshes] = makeNode(gltfNode, gltfModel, filename);
		rootNodes.push_back(childNode);
		for (auto& mesh : childMeshes) {
			loadedMeshes.emplace_back(mesh);
		}
	}
	renderer->setRootNodes(rootNodes);
	renderer->setMeshes(loadedMeshes);
}

int main(size_t argc, const char* argv[]) {

	if (argc < 2) {
		std::cout << "Please input filename" << std::endl;
		return 1;
	}

	vkfw::init();

	vkfw::Window window = vkfw::createWindow(1280, 720, "Hello Vulkan", {}, {});
	window.set<vkfw::Attribute::eResizable>(false);

	vk::ApplicationInfo appInfo{ "Sam's Vulkan Renderer", 1, "Custom Engine", 1, VK_API_VERSION_1_1 };
#ifdef _DEBUG
	const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
#else 
	const std::vector<const char*> validationLayers = { };
#endif
	std::vector<const char*> vulkanExtensions;
	{
		uint32_t c;
		const char** v = vkfw::getRequiredInstanceExtensions(&c);
		vulkanExtensions.resize(c);
		memcpy(vulkanExtensions.data(), v, sizeof(char**) * c);
	}

	vk::Instance vulkanInstance = vk::createInstance(vk::InstanceCreateInfo{ {}, &appInfo, validationLayers, vulkanExtensions });
	vk::SurfaceKHR surface = vkfw::createWindowSurface(vulkanInstance, window);

	renderer = std::make_unique<VulkanRenderer>(vulkanInstance, surface, RendererSettings{});

	std::array<TextureInfo, 6> cubeFaces = {
		loadTexture("./environment/px.png"),
		loadTexture("./environment/nx.png"),
		loadTexture("./environment/py.png"),
		loadTexture("./environment/ny.png"),
		loadTexture("./environment/pz.png"),
		loadTexture("./environment/nz.png"),
	};
	renderer->setEnvironmentMap(cubeFaces);

	std::vector<PointLight> pointLights{ 
		{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
		{{4.0f, 1.15f, -1.74f}, {70.0f, 24.0f, 2.0f}},
		{{4.0f, 1.15f, 1.15f}, {72.0f, 26.0f, 1.0f}},
		{{-5.0f, 1.15f, 1.15f}, {67.0f, 22.0f, 2.0f}},
		{{-5.0f, 1.15f, -1.74f}, {77.0f, 24.0f, 3.0f}},
	};

	DirectionalLight directionalLight = { {0.0f, 100.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, glm::quatLookAt(glm::normalize(glm::vec3{ 0.05f, -1.0f, 0.05f }), glm::vec3{ 1.0f, 0.0f, 0.0f }) };
	/*for (int i = 0; i < 5; i++) {
		const float angle = glm::radians(360.0f) / 5 * i;
		const float radius = 3.0f;
		lights.push_back(PointLight{ {radius * glm::sin(angle), 2.0f, radius * glm::cos(angle)}, {100.0f, 100.0f, 100.0f} });
	}*/
	renderer->setLights(pointLights, directionalLight);

	openGltf(argv[1]);

	renderer->start();

	double runningTime = 0.0;
	auto frameTime = std::chrono::high_resolution_clock::now();
	double deltaTime = 0.0;

	const float moveSpeed = 1.5f;
	const float panSpeed = 3.0f / std::min(window.getHeight(), window.getWidth());
	const float tiltSpeed = 0.001f;

	std::set<vkfw::Key> pressedKeys{};
	std::set<vkfw::MouseButton> pressedMouseButtons{};

	window.callbacks()->on_key = [&pressedKeys](vkfw::Window const&, vkfw::Key key, int32_t, vkfw::KeyAction action, vkfw::ModifierKeyFlags modifiers) {
		if (action == vkfw::KeyAction::ePress) {
			pressedKeys.insert(key);
		}
		if (action == vkfw::KeyAction::eRelease) {
			pressedKeys.erase(key);
		}

		if (action == vkfw::KeyAction::ePress || action == vkfw::KeyAction::eRepeat) {
			if ((key == vkfw::Key::eEqual && modifiers & vkfw::ModifierKeyBits::eShift) || key == vkfw::Key::eKeyPad_Add)
				renderer->settings().exposureBias += 0.5;

			if (key == vkfw::Key::eMinus || key == vkfw::Key::eKeyPad_Subtract)
				renderer->settings().exposureBias -= 0.5;

			if (key == vkfw::Key::eLeftBracket)
				renderer->camera().setVFov(renderer->camera().vfov() + 1.0f);

			if (key == vkfw::Key::eRightBracket)
				renderer->camera().setVFov(renderer->camera().vfov() - 1.0f);
		}
	};

	glm::vec2 lastCursorPos{ 0.0f, 0.0f };

	window.callbacks()->on_mouse_button = [&pressedMouseButtons, &lastCursorPos](vkfw::Window const& window, vkfw::MouseButton button, vkfw::MouseButtonAction action, vkfw::ModifierKeyFlags modifiers) {
		if (action == vkfw::MouseButtonAction::ePress) {
			pressedMouseButtons.insert(button);
			lastCursorPos = { window.getCursorPosX(), window.getCursorPosY() };
		}
		if (action == vkfw::MouseButtonAction::eRelease) {
			pressedMouseButtons.erase(button);
		}
	};
	
	window.callbacks()->on_scroll = [](vkfw::Window const&, double, double scrollY) {
		renderer->camera().dolly(scrollY);
	};

	while (!window.shouldClose()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(6));
		vkfw::pollEvents();

		auto newFrameTime = std::chrono::high_resolution_clock::now();
		deltaTime = std::chrono::duration<double>(newFrameTime - frameTime).count();
		runningTime += deltaTime;
		frameTime = newFrameTime;

		if (pressedMouseButtons.count(vkfw::MouseButton::eRight)) {
			glm::vec2 cursorPos = { window.getCursorPosX(), window.getCursorPosY() };
			auto cursorDelta = cursorPos - lastCursorPos;
			cursorDelta.y = -cursorDelta.y;
			lastCursorPos = cursorPos;
			
			renderer->camera().pan(-panSpeed * cursorDelta);
		}
		else {
			glm::vec3 translation{ 0.0f };
			if (pressedKeys.count(vkfw::Key::eW))
				translation += glm::vec3(0.0f, 0.0f, -1.0f);
			if (pressedKeys.count(vkfw::Key::eA))
				translation += glm::vec3(-1.0f, 0.0f, 0.0f);
			if (pressedKeys.count(vkfw::Key::eS))
				translation += glm::vec3(0.0f, 0.0f, 1.0f);
			if (pressedKeys.count(vkfw::Key::eD))
				translation += glm::vec3(1.0f, 0.0f, 0.0f);

			float translationMag = glm::length(translation);

			if (translationMag > 0.0f)
				renderer->camera().move(static_cast<float>(moveSpeed * deltaTime) * translation / std::min(translationMag, 1.0f));
		}

		if (pressedMouseButtons.count(vkfw::MouseButton::eMiddle)) {
			glm::vec2 cursorPos = { window.getCursorPosX(), window.getCursorPosY() };
			auto cursorDelta = cursorPos - lastCursorPos;
			lastCursorPos = cursorPos;

			renderer->camera().tilt(-tiltSpeed * cursorDelta);
		}
	}
	vkfw::terminate();
}