#include <thread>
#include <shared_mutex>
#include <tuple>
#include <array>

#include <vulkan/vulkan.hpp>

#include <glm/gtx/transform.hpp>

#include <vk_mem_alloc.hpp>

#include "Mesh.h"
#include "PointLight.h"
#include "Texture.h"
#include "Camera.h"

typedef unsigned char byte;

const uint32_t FRAMES_IN_FLIGHT = 2;

typedef struct TextureInfo {
	std::vector<byte> data = {0xff, 0xff, 0xff, 0xff};
	uint32_t width = 1;
	uint32_t height = 1;
} TextureInfo;

enum class MeshSortingMode {
	eNone,
	eFrontToBack,
	eBackToFront
};

typedef struct RendererSettings {
	bool hdrEnabled = true;

	bool dynamicShadowsEnabled = true;
	uint32_t shadowMapResolution = 1024u;

	bool vsync = false;

	float exposure = 0.0f;
	float gamma = 2.2f;
};

typedef struct CameraData {
	glm::vec4 position;
	glm::mat4 viewMatrix;
	glm::mat4 viewProjectionMatrix;
	glm::mat4 invViewProjectionMatrix;
} CameraData;

typedef struct PointLightData {
	alignas(16) glm::vec3 point;
	alignas(16) glm::vec3 intensity;
};

#pragma once
class VulkanRenderer
{
public:
	VulkanRenderer(const vk::Instance vulkanInstance, const vk::SurfaceKHR surface, const RendererSettings& rendererSettings);
	~VulkanRenderer();
	void start();

	void setRootNodes(std::vector<std::shared_ptr<Node>> nodes);
	void setMeshes(const std::vector<Mesh>& meshes);
	void setEnvironmentMap(const std::array<TextureInfo, 6>& textureInfos);
	void setLights(const std::vector<PointLight>& lights);

	Camera& camera() { return this->_camera; };

	RendererSettings& const settings() { return this->_settings; }

private:
	bool running = false;

	RendererSettings _settings;
	
	Camera _camera{ { 0.0f, 0.0f, 0.0f }, {0.0f, 0.0f, 0.0f} };

	vk::Instance vulkanInstance;
	vk::SurfaceKHR surface;

	vk::PhysicalDevice physicalDevice;
	vk::Device device;
	uint32_t graphicsQueueFamilyIndex;
	vk::Queue graphicsQueue;
	vma::Allocator allocator;

	vk::Format swapchainFormat;
	vk::Format colorAttachmentFormat = vk::Format::eR8G8B8A8Unorm;
	vk::Format depthAttachmentFormat = vk::Format::eUndefined;
	vk::Format envMapFormat = vk::Format::eR8G8B8A8Unorm;

	vk::SwapchainKHR swapchain;
	std::vector<vk::ImageView> swapchainImageViews;
	vk::Extent2D swapchainExtent;
	std::vector<vk::Framebuffer> swapchainFramebuffers;

	vk::Image colorImage;
	vk::ImageView colorImageView;
	vma::Allocation colorImageAllocation;

	vk::Image depthImage;
	vk::ImageView depthImageView;
	vma::Allocation depthImageAllocation;

	vk::CommandPool commandPool;
	std::array<vk::CommandBuffer, FRAMES_IN_FLIGHT> commandBuffers;

	vk::RenderPass renderPass;
	vk::PipelineCache pipelineCache;
	vk::PipelineLayout pipelineLayout;
	vk::Pipeline opaquePipeline;
	vk::Pipeline blendPipeline;
	vk::Pipeline wireframePipeline;
	vk::PipelineLayout tonemapPipelineLayout;
	vk::Pipeline tonemapPipeline;

	vk::DescriptorPool descriptorPool;
	vk::DescriptorSetLayout globalDescriptorSetLayout;
	vk::DescriptorSetLayout cameraDescriptorSetLayout;
	vk::DescriptorSetLayout pbrDescriptorSetLayout;
	vk::DescriptorSetLayout tonemapDescriptorSetLayout;
	vk::DescriptorSet globalDescriptorSet;
	std::array<vk::DescriptorSet, FRAMES_IN_FLIGHT> cameraDescriptorSets;
	vk::DescriptorSet tonemapDescriptorSet;

	vk::Buffer vertexIndexBuffer;
	vma::Allocation vertexIndexBufferAllocation;
	size_t vertexBufferOffset;
	size_t indexBufferOffset;

	std::array <vk::Buffer, FRAMES_IN_FLIGHT> cameraBuffers;
	std::array <vma::Allocation, FRAMES_IN_FLIGHT> cameraBufferAllocations;

	std::vector<PointLight> lights;
	vk::Buffer lightsBuffer;
	vma::Allocation lightsBufferAllocation;

	vk::Buffer pbrBuffer;
	vma::Allocation pbrBufferAllocation;
	vk::Buffer alphaBuffer;
	vma::Allocation alphaBufferAllocation;

	vk::Pipeline envPipeline;
	vk::PipelineLayout envPipelineLayout;

	vk::Image envMapImage;
	vk::ImageView envMapImageView;
	vma::Allocation envMapAllocation;

	uint32_t envMapDiffuseResolution = 256u;
	vk::Image envMapDiffuseImage;
	vk::ImageView envMapDiffuseImageView;
	vma::Allocation envMapDiffuseAllocation;

	uint32_t envMapSpecularResolution = 512u;
	vk::Image envMapSpecularImage;
	vk::ImageView envMapSpecularImageView;
	vma::Allocation envMapSpecularAllocation;

	Texture BRDFLutTexture;

	std::vector<Texture> textures;
	vk::Sampler textureSampler;

	std::vector<vk::ShaderModule> shaderModules{};

	std::array<vk::Fence, FRAMES_IN_FLIGHT> frameFences;
	std::array<vk::Semaphore, FRAMES_IN_FLIGHT> imageAcquiredSemaphores;
	std::array<vk::Semaphore, FRAMES_IN_FLIGHT> renderFinishedSemaphores;
	std::array<vk::Semaphore, FRAMES_IN_FLIGHT> shadowPassFinishedSemaphores;

	std::thread renderThread;

	std::shared_mutex vertexBufferMutex;
	
	std::vector<std::shared_ptr<Mesh>> opaqueMeshes;
	std::vector<std::shared_ptr<Mesh>> nonOpaqueMeshes;
	std::vector<std::shared_ptr<Mesh>> alphaMaskMeshes;
	std::vector<std::shared_ptr<Mesh>> alphaBlendMeshes;
	std::vector<std::shared_ptr<Mesh>> boundingBoxMeshes;
	std::vector<std::shared_ptr<Mesh>> staticMeshes;
	std::vector<std::shared_ptr<Mesh>> dynamicMeshes;

	std::vector<std::shared_ptr<Mesh>> meshes;

	std::vector<std::shared_ptr<Node>> rootNodes;

	vk::RenderPass shadowMapRenderPass;
	vk::RenderPass staticShadowMapRenderPass;
	std::array<vk::CommandBuffer, FRAMES_IN_FLIGHT> shadowPassCommandBuffers;
	vk::PipelineLayout shadowMapPipelineLayout;
	vk::Pipeline shadowMapPipeline;
	vk::Sampler shadowMapSampler;
	vk::Image shadowMapImage;
	vk::Image staticShadowMapImage;
	vma::Allocation shadowMapImageAllocation;
	vma::Allocation staticShadowMapImageAllocation;
	vk::ImageView shadowMapCubeArrayImageView;
	std::vector<vk::ImageView> shadowMapFaceImageViews;
	std::vector<vk::ImageView> staticShadowMapFaceImageViews;
	std::vector<vk::Framebuffer> shadowMapFramebuffers;
	std::vector<vk::Framebuffer> staticShadowMapFramebuffers;

	
	void setPhysicalDevice(vk::PhysicalDevice physicalDevice);

	void createSwapchain();
	void createRenderPass();

	void createDescriptorPool();
	vk::ShaderModule loadShader(const std::string& path);
	void createPipeline();
	void createTonemapPipeline();
	void createVertexBuffer();
	void destroyVertexBuffer();
	void createEnvPipeline();

	void createShadowMapImage();
	void createStaticShadowMapImage();
	void createShadowMapRenderPass();
	void createStaticShadowMapRenderPass();
	void createShadowMapPipeline();
	void renderShadowMaps(uint32_t frameIndex);

	void makeDiffuseEnvMap();
	std::tuple<vk::Pipeline, vk::PipelineLayout> createEnvMapDiffuseBakePipeline(vk::RenderPass renderPass);
	std::tuple<vk::RenderPass, std::array<vk::ImageView, 6>, std::array<vk::Framebuffer, 6>> createEnvMapDiffuseBakeRenderPass();
	void makeSpecularEnvMap();
	std::tuple<vk::Pipeline, vk::PipelineLayout> createEnvMapSpecularBakePipeline(vk::RenderPass renderPass);
	std::tuple<vk::RenderPass, std::array<std::array<vk::ImageView, 10>, 6>, std::array<std::array<vk::Framebuffer, 10>, 6>> createEnvMapSpecularBakeRenderPass();

	void renderLoop();
	void drawMeshes(const std::vector<std::shared_ptr<Mesh>>& meshes, const vk::CommandBuffer& cb, uint32_t frameIndex, const glm::mat4& viewproj, const glm::vec3& cameraPos, bool frustumCull = false, MeshSortingMode sortingMode = MeshSortingMode::eNone);

	std::tuple<vk::Image, vk::ImageView, vma::Allocation> createImageFromTextureInfo(TextureInfo& textureInfo);
};

