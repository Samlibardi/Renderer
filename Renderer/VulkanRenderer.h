#include <thread>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.hpp>
#include <vkfw/vkfw.hpp>

#include <vk_mem_alloc.hpp>

#include "Mesh.h"
#include "PointLight.h"

#pragma once
class VulkanRenderer
{
public:
	VulkanRenderer();
	~VulkanRenderer();
	void run();

	void setMesh(Mesh & mesh);
	void setTexture(std::vector<byte> & imageBinary, uint32_t width, uint32_t height);
	void setLights(std::vector<PointLight> lights);


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

	vk::DescriptorSetLayout descriptorSetLayout;
	vk::DescriptorPool descriptorPool;
	std::vector<vk::DescriptorSet> descriptorSets;

	vk::Buffer vertexBuffer;
	vk::Buffer indexBuffer;
	vk::DeviceMemory vertexBufferMemory;

	vk::Buffer lightsBuffer;
	vk::DeviceMemory lightsBufferMemory;

	vk::Image textureImage;
	vk::ImageView textureImageView;
	vk::DeviceMemory textureMemory;
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
};

