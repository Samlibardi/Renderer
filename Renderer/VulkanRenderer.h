#pragma once

#include <thread>
#include <shared_mutex>
#include <tuple>
#include <array>

#include <vulkan/vulkan.hpp>
#include <vkfw/vkfw.hpp>

#include <glm/gtx/transform.hpp>

#include <vma/vk_mem_alloc.hpp>

#include "Mesh.h"
#include "PointLight.h"
#include "DirectionalLight.h"
#include "Texture.h"
#include "Camera.h"
#include "Buffer.h"

typedef unsigned char byte;

const uint32_t FRAMES_IN_FLIGHT = 2;

struct TextureInfo {
	std::vector<byte> data = {0xff, 0xff, 0xff, 0xff};
	uint32_t width = 1;
	uint32_t height = 1;
};

enum class SampleCount : uint32_t {
	e1 = 1u,
	e2 = 2u,
	e4 = 4u,
	e8 = 8u,
	e16 = 16u,
	e32 = 32u,
	e64 = 64u,
};

struct RendererSettings {
	bool hdrOutputEnabled = false;

	bool dynamicShadowsEnabled = true;
	uint32_t pointShadowMapResolution = 512u;
	uint16_t pointShadowMapCount = 8u;
	uint32_t directionalShadowMapResolution = 2048u;
	uint8_t directionalShadowCascadeLevels = 4u;

	bool vsync = false;

	float exposureBias = 0.0f;
	float gamma = 2.2f;

	SampleCount msaa = SampleCount::e4;
};

class VulkanRenderer
{
	struct CameraShaderData {
		glm::vec4 position;
		glm::mat4 viewProjectionMatrix;
		glm::mat4 invViewProjectionMatrix;
	};

	struct PointLightShaderData {
		alignas(16) glm::vec3 position;
		alignas(16) glm::vec3 intensity;
		PointLightFlags flags;
		uint16_t shadowMapIndex;
	};

	struct DirectionalLightShaderData {
		alignas(16) glm::vec3 position;
		alignas(16) glm::vec3 direction;
		alignas(16) glm::vec3 intensity;
	};

	struct CSMSplitShaderData {
		glm::mat4 viewproj;
		alignas(16) float depth;
	};

	enum class MeshSortingMode {
		eNone,
		eFrontToBack,
		eBackToFront
	};

public:
	VulkanRenderer(const vkfw::Window window, const RendererSettings& rendererSettings);
	VulkanRenderer(const VulkanRenderer& other) = delete;
	VulkanRenderer(VulkanRenderer&& other) = default;
	~VulkanRenderer();
	void start();

	void setRootNodes(std::vector<std::shared_ptr<Node>> nodes);
	void setEnvironmentMap(const std::array<TextureInfo, 6>& textureInfos);
	void setLights(const std::vector<PointLight>& pointLights, const DirectionalLight& directionalLight);
	Buffer loadBuffer(const void* _ptr, size_t size);
	void addMesh(const Mesh& mesh);

	Camera& camera() { return this->_camera; };

	RendererSettings& settings() { return this->_settings; }

private:
	bool running = false;

	RendererSettings _settings;
	
	Camera _camera = Camera::Perspective(glm::vec3{ 0.0f }, glm::vec3{ 0.0f }, 0.1f, 60.0f, glm::radians(40.0f), 1.0f);

	vk::Instance vulkanInstance;
	vk::SurfaceKHR surface;

	vk::PhysicalDevice physicalDevice;
	vk::Device device;
	uint32_t graphicsQueueFamilyIndex;
	vk::Queue graphicsQueue;
	vma::Allocator allocator;

	vk::Format swapchainFormat;
	vk::Format colorAttachmentFormat = vk::Format::eUndefined;
	vk::Format depthAttachmentFormat = vk::Format::eUndefined;
	vk::Format envMapFormat = vk::Format::eUndefined;
	vk::Format bloomAttachmentFormat = vk::Format::eUndefined;
	vk::Format averageLuminanceFormat = vk::Format::eUndefined;

	vk::SwapchainKHR swapchain;
	std::vector<vk::Image> swapchainImages;
	std::vector<vk::ImageView> swapchainImageViews;
	vk::Extent2D swapchainExtent;

	vk::Image colorImage;
	vk::ImageView colorImageView;
	vma::Allocation colorImageAllocation;

	vk::Image depthImage;
	vk::ImageView depthImageView;
	vma::Allocation depthImageAllocation;

	vk::Image colorImageMS;
	vk::ImageView colorImageMSView;
	vma::Allocation colorImageMSAllocation;

	vk::Image luminanceImage;
	vk::ImageView luminanceImageView;
	vma::Allocation luminanceImageAllocation;

	vk::Framebuffer mainFramebuffer;

	vk::CommandPool commandPool;
	std::array<vk::CommandBuffer, FRAMES_IN_FLIGHT> mainCommandBuffers;

	vk::RenderPass renderPass;
	vk::PipelineCache pipelineCache;
	vk::PipelineLayout pipelineLayout;
	vk::Pipeline opaquePipeline;
	vk::Pipeline blendPipeline;
	vk::Pipeline wireframePipeline;
	vk::PipelineLayout tonemapPipelineLayout;
	vk::Pipeline tonemapPipeline;
	vk::Sampler tonemapSampler;

	std::array<vk::CommandBuffer, FRAMES_IN_FLIGHT> tonemapCommandBuffers;

	vk::DescriptorPool descriptorPool;
	vk::DescriptorSetLayout globalDescriptorSetLayout;
	vk::DescriptorSet globalDescriptorSet;
	vk::DescriptorSetLayout perFrameInFlightDescriptorSetLayout;
	vk::DescriptorSetLayout envDescriptorSetLayout;
	vk::DescriptorSet envDescriptorSet;
	vk::DescriptorSetLayout pbrDescriptorSetLayout;
	std::array<vk::DescriptorSet, FRAMES_IN_FLIGHT> perFrameInFlightDescriptorSets;
	vk::DescriptorSetLayout tonemapDescriptorSetLayout;
	std::vector<vk::DescriptorSet> tonemapDescriptorSets;

	uint32_t nextBufferId = 0u;
	std::unordered_map<uint32_t, std::tuple<vk::Buffer, vma::Allocation>> bufferTable;

	std::array <vk::Buffer, FRAMES_IN_FLIGHT> cameraBuffers;
	std::array <vma::Allocation, FRAMES_IN_FLIGHT> cameraBufferAllocations;

	std::vector<std::shared_ptr<PointLight>> pointLights;
	std::vector<std::shared_ptr<PointLight>> shadowCastingPointLights;
	DirectionalLight directionalLight;
	vk::Buffer lightsBuffer;
	vma::Allocation lightsBufferAllocation;
	vk::Buffer lightsStagingBuffer;
	vma::Allocation lightsStagingBufferAllocation;

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
	vk::Sampler averageLuminanceSampler;

	std::vector<vk::ShaderModule> shaderModules{};

	std::array<vk::Fence, FRAMES_IN_FLIGHT> frameFences;
	std::array<vk::Semaphore, FRAMES_IN_FLIGHT> imageAcquiredSemaphores;
	std::array<vk::Semaphore, FRAMES_IN_FLIGHT> mainRenderPassFinishedSemaphores;
	std::array<vk::Semaphore, FRAMES_IN_FLIGHT> shadowPassFinishedSemaphores;
	std::array<vk::Semaphore, FRAMES_IN_FLIGHT> bloomPassFinishedSemaphores;
	std::array<vk::Semaphore, FRAMES_IN_FLIGHT> compositionPassFinishedSemaphores;

	std::thread renderThread;

	std::shared_mutex vertexBufferMutex;
	
	std::vector<std::shared_ptr<MeshPrimitive>> opaqueMeshes;
	std::vector<std::shared_ptr<MeshPrimitive>> nonOpaqueMeshes;
	std::vector<std::shared_ptr<MeshPrimitive>> alphaMaskMeshes;
	std::vector<std::shared_ptr<MeshPrimitive>> alphaBlendMeshes;
	std::vector<std::shared_ptr<MeshPrimitive>> boundingBoxMeshes;
	std::vector<std::shared_ptr<MeshPrimitive>> staticMeshes;
	std::vector<std::shared_ptr<MeshPrimitive>> dynamicMeshes;

	std::vector<Mesh> meshes;

	std::vector<std::shared_ptr<Node>> rootNodes;

	vk::RenderPass shadowMapRenderPass;
	vk::RenderPass staticShadowMapRenderPass;
	std::array<vk::CommandBuffer, FRAMES_IN_FLIGHT> shadowPassCommandBuffers;
	vk::PipelineLayout shadowMapPipelineLayout;
	vk::Pipeline shadowMapPipeline;
	vk::Sampler shadowMapSampler;
	vk::Image pointShadowMapsImage;
	vk::Image staticPointShadowMapsImage;
	vma::Allocation pointShadowMapsImageAllocation;
	vma::Allocation pointStaticShadowMapsImageAllocation;
	vk::ImageView pointShadowMapCubeArrayImageView;
	std::vector<vk::ImageView> pointShadowMapFaceImageViews;
	std::vector<vk::ImageView> staticPointShadowMapFaceImageViews;
	std::vector<vk::Framebuffer> pointShadowMapFramebuffers;
	std::vector<vk::Framebuffer> staticPointShadowMapFramebuffers;

	vk::RenderPass directionalShadowMapRenderPass;
	std::vector<float> directionalShadowCascadeDepths;
	std::vector<float> directionalShadowCascadeCameraSpaceDepths;
	std::array<vk::Buffer, FRAMES_IN_FLIGHT> directionalShadowCascadeSplitDataBuffers;
	std::array<vma::Allocation, FRAMES_IN_FLIGHT> directionalShadowCascadeSplitDataBufferAllocations;
	std::vector<glm::mat4> directionalShadowCascadeCropMatrices;
	vk::Image directionalCascadedShadowMapsImage;
	vma::Allocation directionalCascadedShadowMapsImageAllocation;
	std::vector<vk::ImageView> directionalShadowMapImageViews;
	std::vector<vk::Framebuffer> directionalShadowMapFramebuffers;
	vk::ImageView directionalShadowMapArrayImageView;


	vk::PipelineLayout averageLuminancePipelineLayout;
	vk::Pipeline averageLuminancePipeline;
	vk::DescriptorSetLayout averageLuminanceDescriptorSetLayout;
	std::vector<vk::DescriptorSet> averageLuminanceDescriptorSets;

	std::vector<vk::CommandBuffer> averageLuminanceCommandBuffers;

	vk::Image averageLuminance256Image;
	vma::Allocation averageLuminance256ImageAllocation;
	vk::ImageView averageLuminance256ImageView;
	vk::Image averageLuminance16Image;
	vma::Allocation averageLuminance16ImageAllocation;
	vk::ImageView averageLuminance16ImageView;
	vk::Image averageLuminance1Image;
	vma::Allocation averageLuminance1ImageAllocation;
	vk::ImageView averageLuminance1ImageView;
	vk::Buffer averageLuminanceHostBuffer;
	vma::Allocation averageLuminanceHostBufferAllocation;


	float temporalLuminance = 0.0f;

	vk::PipelineLayout bloomPipelineLayout;
	vk::Pipeline bloomDownsamplePipeline;
	vk::Pipeline bloomUpsamplePipeline;
	vk::DescriptorSetLayout bloomDescriptorSetLayout;
	std::vector<vk::DescriptorSet> bloomDownsampleDescriptorSets;
	std::vector<vk::DescriptorSet> bloomUpsampleDescriptorSets;

	std::array<vk::CommandBuffer, FRAMES_IN_FLIGHT> bloomCommandBuffers;

	vk::Image bloomImage;
	vma::Allocation bloomImageAllocation;
	uint32_t bloomMipLevels = 6;
	vk::Sampler bloomDownsampleSampler;
	vk::Sampler bloomUpsampleSampler;
	std::vector<vk::ImageView> bloomImageViews;

	void setPhysicalDevice(vk::PhysicalDevice physicalDevice);

	void createSwapchainAndAttachmentImages();
	void createRenderPass();

	void createDescriptorPool();
	vk::ShaderModule loadShader(const std::string& path);
	void createPipeline();
	void createTonemapPipeline();
	void createEnvPipeline();
	
	void createAverageLuminancePipeline();
	void createAverageLuminanceImages();
	void recordAverageLuminanceCommands();

	void createBloomPipelines();
	void createBloomImage();
	void recordBloomCommandBuffers();

	void createShadowMapImage();
	void createStaticShadowMapImage();
	void createShadowMapRenderPass();
	void createDirectionalShadowMapRenderPass();
	void createStaticShadowMapRenderPass();
	void createShadowMapPipeline();
	void renderShadowMaps(uint32_t frameIndex, const glm::vec3& cameraPos);
	void recordPointShadowMapsCommands(vk::CommandBuffer cb, uint32_t frameIndex, const glm::vec3& cameraPos);
	void recordDirectionalShadowMapsCommands(vk::CommandBuffer cb, uint32_t frameIndex);

	void makeDiffuseEnvMap();
	std::tuple<vk::Pipeline, vk::PipelineLayout> createEnvMapDiffuseBakePipeline(vk::RenderPass renderPass);
	std::tuple<vk::RenderPass, std::array<vk::ImageView, 6>, std::array<vk::Framebuffer, 6>> createEnvMapDiffuseBakeRenderPass();
	void makeSpecularEnvMap();
	std::tuple<vk::Pipeline, vk::PipelineLayout> createEnvMapSpecularBakePipeline(vk::RenderPass renderPass);
	std::tuple<vk::RenderPass, std::array<std::array<vk::ImageView, 10>, 6>, std::array<std::array<vk::Framebuffer, 10>, 6>> createEnvMapSpecularBakeRenderPass();

	void recordUpdateLightsBufferCommands(const vk::CommandBuffer& cb);

	void renderLoop();
	void drawMeshes(const std::vector<std::shared_ptr<MeshPrimitive>>& meshes, const vk::CommandBuffer& cb, uint32_t frameIndex, const glm::mat4& viewproj, const glm::vec3& cameraPos, bool frustumCull = false, MeshSortingMode sortingMode = MeshSortingMode::eNone);
	
	bool cullMesh(const MeshPrimitive& mesh);
	bool cullMesh(const MeshPrimitive& mesh, const Camera& pov);

	std::tuple<vk::Image, vk::ImageView, vma::Allocation> createImageFromTextureInfo(TextureInfo& textureInfo);
};

