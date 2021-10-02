#include <thread>
#include <tuple>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>
#include <vkfw/vkfw.hpp>

#include <glm/vec4.hpp>

#include <vk_mem_alloc.hpp>

#include "Mesh.h"
#include "PointLight.h"

typedef struct TextureInfo {
	std::vector<byte> data = {0xff, 0xff, 0xff, 0xff};
	uint32_t width = 1;
	uint32_t height = 1;
} TextureInfo;

typedef struct {
	glm::vec4 baseColorFactor;
	glm::vec4 emissiveFactor;
	float normalScale;
	float metallicFactor;
	float roughnessFactor;
	float aoFactor;
} PBRInfo;

#pragma once
class VulkanRenderer
{
public:
	VulkanRenderer();
	~VulkanRenderer();
	void run();

	void setMesh(Mesh & mesh);
	void setTextures(TextureInfo albedo, TextureInfo normal, TextureInfo metallicRoughness, TextureInfo ao, TextureInfo emissive);
	void setLights(std::vector<PointLight> lights);
	void setPBRParameters(PBRInfo parameters);

private:
	bool running = false;
	
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
	std::vector<vk::DescriptorSet> descriptorSets;

	vk::Buffer vertexBuffer;
	vma::Allocation vertexBufferAllocation;
	vk::Buffer indexBuffer;
	vma::Allocation indexBufferAllocation;

	vk::Buffer lightsBuffer;
	vk::DeviceMemory lightsBufferMemory;

	vk::Buffer pbrBuffer;
	vma::Allocation pbrBufferAllocation;

	std::vector<std::tuple<vk::Image, vk::ImageView, vma::Allocation>> textures;
	vk::Sampler textureSampler;

	std::vector<vk::ShaderModule> shaderModules;

	std::vector<vk::Semaphore> acquireImageSemaphores;
	std::vector<vk::Semaphore> presentSemaphores;
	std::vector<vk::Fence> frameFences;

	std::thread renderThread;
	
	Mesh mesh;
	
	void renderLoop();
	void setPhysicalDevice(vk::PhysicalDevice physicalDevice);
	void createSwapchain();
	void createRenderPass();
	void createPipeline();
	void createVertexBuffer();
	void destroyVertexBuffer();

	std::tuple<vk::Image, vk::ImageView, vma::Allocation> createImageFromTextureInfo(TextureInfo& textureInfo);
	void stageTexture(vk::Image image, TextureInfo& textureInfo);
};

