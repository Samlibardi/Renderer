#include <thread>
#include <shared_mutex>
#include <tuple>
#include <array>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>
#include <vkfw/vkfw.hpp>

#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtx/transform.hpp>

#include <vk_mem_alloc.hpp>

#include "Mesh.h"
#include "PointLight.h"
#include "Texture.h"
#include "Camera.h"

typedef struct TextureInfo {
	std::vector<byte> data = {0xff, 0xff, 0xff, 0xff};
	uint32_t width = 1;
	uint32_t height = 1;
} TextureInfo;

#pragma once
class VulkanRenderer
{
public:
	VulkanRenderer(vkfw::Window window);
	~VulkanRenderer();
	void start();

	void setMeshes(const std::vector<Mesh>);
	void setTextures(Texture albedo, Texture normal, Texture metallicRoughness, Texture ao, Texture emissive);
	void setEnvironmentMap(std::array<TextureInfo, 6> faces);
	void setLights(std::vector<PointLight> lights);

	Camera& camera() { return this->_camera; };

private:
	bool running = false;
	
	Camera _camera{ { 0.0f, 0.5f, 8.0f }, {0.0f, 0.0f, 0.0f} };

	vkfw::Window window;
	vk::Instance vulkanInstance;
	vk::SurfaceKHR surface;

	vk::PhysicalDevice physicalDevice;
	vk::Device device;
	uint32_t graphicsQueueFamilyIndex;
	vk::Queue graphicsQueue;
	vma::Allocator allocator;

	vk::SwapchainKHR swapchain;
	std::vector<vk::ImageView> swapchainImageViews;
	vk::Extent2D swapchainExtent;
	vk::Format swapchainFormat;
	std::vector<vk::Framebuffer> swapchainFramebuffers;

	vk::Image depthImage;
	vk::ImageView depthImageView;
	vk::DeviceMemory depthImageMemory;

	vk::CommandPool commandPool;
	std::vector<vk::CommandBuffer> commandBuffers;

	vk::RenderPass renderPass;
	vk::Pipeline pipeline;
	vk::PipelineLayout pipelineLayout;

	vk::DescriptorPool descriptorPool;
	vk::DescriptorSetLayout globalDescriptorSetLayout;
	vk::DescriptorSetLayout pbrDescriptorSetLayout;
	vk::DescriptorSet globalDescriptorSet;

	vk::Buffer vertexIndexBuffer;
	vma::Allocation vertexIndexBufferAllocation;
	size_t vertexBufferOffset;
	size_t indexBufferOffset;

	vk::Buffer lightsBuffer;
	vk::DeviceMemory lightsBufferMemory;

	vk::Buffer pbrBuffer;
	vma::Allocation pbrBufferAllocation;

	vk::Pipeline envPipeline;
	vk::PipelineLayout envPipelineLayout;

	vk::Image envMapImage;
	vk::ImageView envMapImageView;
	vma::Allocation envMapAllocation;

	uint32_t envMapDiffuseResolution = 1024;
	vk::Image envMapDiffuseImage;
	vk::ImageView envMapDiffuseImageView;
	vma::Allocation envMapDiffuseAllocation;

	uint32_t envMapSpecularResolution = 1024;
	vk::Image envMapSpecularImage;
	vk::ImageView envMapSpecularImageView;
	vma::Allocation envMapSpecularAllocation;

	Texture BRDFLutTexture;

	std::vector<Texture> textures;
	vk::Sampler textureSampler;

	std::vector<vk::ShaderModule> shaderModules{};

	std::vector<vk::Semaphore> acquireImageSemaphores;
	std::vector<vk::Semaphore> presentSemaphores;
	std::vector<vk::Fence> frameFences;

	std::thread renderThread;

	std::shared_mutex vertexBufferMutex;
	
	std::vector<Mesh> meshes;
	
	void renderLoop();
	void setPhysicalDevice(vk::PhysicalDevice physicalDevice);
	void createSwapchain();
	void createRenderPass();
	void createPipeline();
	void createVertexBuffer();
	void destroyVertexBuffer();
	void createEnvPipeline();
	void makeDiffuseEnvMap();
	std::tuple<vk::Pipeline, vk::PipelineLayout> createEnvMapDiffuseBakePipeline(vk::RenderPass renderPass);
	std::tuple<vk::RenderPass, std::array<vk::ImageView, 6>, std::array<vk::Framebuffer, 6>> createEnvMapDiffuseBakeRenderPass();
	void makeSpecularEnvMap();
	std::tuple<vk::Pipeline, vk::PipelineLayout> createEnvMapSpecularBakePipeline(vk::RenderPass renderPass);
	std::tuple<vk::RenderPass, std::array<std::array<vk::ImageView, 10>, 6>, std::array<std::array<vk::Framebuffer, 10>, 6>> createEnvMapSpecularBakeRenderPass();

	std::tuple<vk::Image, vk::ImageView, vma::Allocation> createImageFromTextureInfo(TextureInfo& textureInfo);
	void stageTexture(vk::Image image, TextureInfo& textureInfo);
};

