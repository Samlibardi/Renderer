#include <fstream>
#include <chrono>

#include <cppitertools/enumerate.hpp>
#include <cppitertools/chain.hpp>
#include <cppitertools/filter.hpp>
#include <cppitertools/sorted.hpp>

#include "VulkanRenderer.h"

#include <glm/gtx/normal.hpp>

#include "Vertex.h"
#include "Mesh.h"
#include "PointLight.h"

#include <iostream>

VulkanRenderer::VulkanRenderer(const vkfw::Window window, const RendererSettings& rendererSettings) : window(window), settings(rendererSettings) {

	vk::ApplicationInfo appInfo{"Custom Vulkan Renderer", 1, "Custom Engine", 1, VK_API_VERSION_1_1};
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
#ifdef _DEBUG
	vulkanExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

	this->vulkanInstance = vk::createInstance(vk::InstanceCreateInfo{{}, &appInfo, validationLayers, vulkanExtensions});
	this->surface = vkfw::createWindowSurface(this->vulkanInstance, this->window);

	std::vector<vk::PhysicalDevice> availableDevices = this->vulkanInstance.enumeratePhysicalDevices();
	if (availableDevices.empty()) throw std::runtime_error("No devices with Vulkan support are available");
	this->setPhysicalDevice(availableDevices.front());
	
	this->textureSampler = this->device.createSampler(vk::SamplerCreateInfo{ {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, 0.0f, true, this->physicalDevice.getProperties().limits.maxSamplerAnisotropy, false, vk::CompareOp::eNever, 0.0f, VK_LOD_CLAMP_NONE});
	
	this->BRDFLutTexture = Texture("./textures/ibl_brdf_lut.png", 1);
	this->BRDFLutTexture.loadToDevice(this->device, this->allocator, this->graphicsQueue, this->commandPool);

	this->createDescriptorPool();

	this->createSwapchain();
	this->createRenderPass();
	
	this->pipelineCache = this->device.createPipelineCache(vk::PipelineCacheCreateInfo{});
	this->createPipeline();
	this->createTonemapPipeline();
	this->createEnvPipeline();

	this->createShadowMapRenderPass();
	this->createStaticShadowMapRenderPass();
	this->createShadowMapPipeline();

	for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		this->frameFences[i] = this->device.createFence(vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
		this->imageAcquiredSemaphores[i] = this->device.createSemaphore(vk::SemaphoreCreateInfo{});
		this->renderFinishedSemaphores[i] = this->device.createSemaphore(vk::SemaphoreCreateInfo{});
		this->shadowPassFinishedSemaphores[i] = this->device.createSemaphore(vk::SemaphoreCreateInfo{});
	}
}

VulkanRenderer::~VulkanRenderer() {
	this->running = false;
	this->renderThread.join();
	this->device.waitForFences(this->frameFences, true, UINT64_MAX);

	for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		this->device.destroyFence(this->frameFences[i]);
		this->device.destroySemaphore(this->imageAcquiredSemaphores[i]);
		this->device.destroySemaphore(this->renderFinishedSemaphores[i]);
		this->device.destroySemaphore(this->shadowPassFinishedSemaphores[i]);
	}
	this->device.destroyPipeline(this->opaquePipeline);
	this->device.destroyPipeline(this->blendPipeline);
	this->device.destroyPipeline(this->tonemapPipeline);
	this->device.destroyPipeline(this->wireframePipeline);
	this->device.destroyPipeline(this->envPipeline);
	this->device.destroyPipeline(this->shadowMapPipeline);
	
	this->device.destroyPipelineCache(this->pipelineCache);

	this->device.destroyPipelineLayout(this->pipelineLayout);
	this->device.destroyPipelineLayout(this->envPipelineLayout);
	this->device.destroyPipelineLayout(this->tonemapPipelineLayout);
	this->device.destroyPipelineLayout(this->shadowMapPipelineLayout);

	for (auto& sm : this->shaderModules) {
		this->device.destroyShaderModule(sm);
	}
	this->shaderModules = {};

	for (auto& fb : this->swapchainFramebuffers) {
		this->device.destroyFramebuffer(fb);
	}
	this->swapchainFramebuffers = {};

	for (auto& fb : this->shadowMapFramebuffers) {
		this->device.destroyFramebuffer(fb);
	}
	this->shadowMapFramebuffers = {};

	for (auto& fb : this->staticShadowMapFramebuffers) {
		this->device.destroyFramebuffer(fb);
	}
	this->staticShadowMapFramebuffers = {};

	this->device.destroyRenderPass(this->renderPass);
	this->device.destroyRenderPass(this->shadowMapRenderPass);
	this->device.destroyRenderPass(this->staticShadowMapRenderPass);

	for (auto& iv : this->swapchainImageViews) {
		this->device.destroyImageView(iv);
	}
	this->swapchainImageViews = {};

	for (auto& iv : this->shadowMapFaceImageViews) {
		this->device.destroyImageView(iv);
	}
	this->shadowMapFaceImageViews = {};
	this->device.destroyImageView(this->shadowMapCubeArrayImageView);

	for (auto& iv : this->staticShadowMapFaceImageViews) {
		this->device.destroyImageView(iv);
	}
	this->staticShadowMapFaceImageViews = {};

	this->device.destroyImageView(this->envMapImageView);
	this->device.destroyImageView(this->envMapDiffuseImageView);
	this->device.destroyImageView(this->envMapSpecularImageView);
	this->device.destroyImageView(this->colorImageView);
	this->device.destroyImageView(this->depthImageView);

	this->allocator.destroyImage(this->shadowMapImage, this->shadowMapImageAllocation);
	this->allocator.destroyImage(this->staticShadowMapImage, this->staticShadowMapImageAllocation);
	this->allocator.destroyImage(this->envMapImage, this->envMapAllocation);
	this->allocator.destroyImage(this->envMapDiffuseImage, this->envMapDiffuseAllocation);
	this->allocator.destroyImage(this->envMapSpecularImage, this->envMapSpecularAllocation);
	this->allocator.destroyImage(this->colorImage, this->colorImageAllocation);
	this->allocator.destroyImage(this->depthImage, this->depthImageAllocation);

	Texture::clearCache();
	this->meshes = {};
	this->opaqueMeshes = {};
	this->alphaBlendMeshes = {};
	this->alphaMaskMeshes = {};
	this->dynamicMeshes = {};
	this->staticMeshes = {};
	
	this->device.destroyDescriptorSetLayout(this->globalDescriptorSetLayout);
	this->device.destroyDescriptorSetLayout(this->pbrDescriptorSetLayout);
	this->device.destroyDescriptorSetLayout(this->cameraDescriptorSetLayout);
	this->device.destroyDescriptorSetLayout(this->tonemapDescriptorSetLayout);

	this->device.destroyDescriptorPool(this->descriptorPool);

	this->device.destroySampler(this->shadowMapSampler);
	this->device.destroySampler(this->textureSampler);

	this->destroyVertexBuffer();

	this->device.destroySwapchainKHR(this->swapchain);
	this->device.destroyCommandPool(this->commandPool);
	this->device.destroy();
	this->vulkanInstance.destroySurfaceKHR(this->surface);
	this->vulkanInstance.destroy();
	vkfw::terminate();
}

std::tuple<vk::Image, vk::ImageView, vma::Allocation> VulkanRenderer::createImageFromTextureInfo(TextureInfo& textureInfo) {
	uint32_t mipLevels = 1;
	//if (textureInfo.width > 512 && textureInfo.height > 512) mipLevels = 4;

	auto [image, allocation] =  this->allocator.createImage(
		vk::ImageCreateInfo{ {}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D{textureInfo.width, textureInfo.height, 1}, mipLevels, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	vk::ImageView imageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Unorm, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } });

	return { image, imageView, allocation };
}

void VulkanRenderer::setLights(const std::vector<PointLight>& lights) {
	if (!lights.size())
		return;

	this->lights = lights;

	size_t bufferSize = lights.size() * sizeof(PointLightData);
	std::tie(this->lightsBuffer, this->lightsBufferAllocation) = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, bufferSize, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly });

	auto [lightsStagingBuffer, lightsStagingBufferAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, bufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuOnly });

	PointLightData* data = reinterpret_cast<PointLightData*>(this->allocator.mapMemory(lightsStagingBufferAllocation));
	for (auto&& [i, light] : iter::enumerate(lights)) {
		data[i] = PointLightData{ light.point, light.intensity };
	}
	this->allocator.unmapMemory(lightsStagingBufferAllocation);

	vk::CommandBuffer cb = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];

	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	cb.copyBuffer(lightsStagingBuffer, this->lightsBuffer, vk::BufferCopy{0, 0, bufferSize});
	cb.end();

	vk::Fence fence = this->device.createFence(vk::FenceCreateInfo{});
	this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
	this->device.waitForFences(fence, true, UINT64_MAX);
	this->allocator.destroyBuffer(lightsStagingBuffer, lightsStagingBufferAllocation);
	this->device.freeCommandBuffers(commandPool, cb);
	this->device.destroyFence(fence);


	std::vector<vk::DescriptorBufferInfo> descriptorBufferInfos = { vk::DescriptorBufferInfo{this->lightsBuffer, 0, bufferSize } };
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = { vk::WriteDescriptorSet{ this->globalDescriptorSet, 0, 0, vk::DescriptorType::eStorageBuffer, {}, descriptorBufferInfos } };
	this->device.updateDescriptorSets(writeDescriptorSets, {});

	this->createShadowMapImage();
	this->createStaticShadowMapImage();
}

void VulkanRenderer::setRootNodes(std::vector<std::shared_ptr<Node>> nodes) {
	this->rootNodes = nodes;
}

void VulkanRenderer::setMeshes(const std::vector<Mesh>& meshes) {
	this->destroyVertexBuffer();
	
	for (const Mesh& mesh : meshes) {
		auto meshPtr = std::make_shared<Mesh>(mesh);
		switch (mesh.alphaInfo.alphaMode) {
		case AlphaMode::eOpaque:
			this->opaqueMeshes.push_back(meshPtr);
			break;
		case AlphaMode::eMask:
			this->alphaMaskMeshes.push_back(meshPtr);
			this->nonOpaqueMeshes.push_back(meshPtr);
			break;
		case AlphaMode::eBlend:
			this->alphaBlendMeshes.push_back(meshPtr);
			this->nonOpaqueMeshes.push_back(meshPtr);
			break;
		}
		if (mesh.node->isStatic())
			this->staticMeshes.push_back(meshPtr);
		else
			this->dynamicMeshes.push_back(meshPtr);
		this->meshes.push_back(meshPtr);
		this->boundingBoxMeshes.push_back(std::make_shared<Mesh>(mesh.boundingBoxAsMesh()));
	}

	vk::DeviceSize minBufferAlignment = this->physicalDevice.getProperties().limits.minUniformBufferOffsetAlignment;
	vk::DeviceSize pbrAlignment = std::max(sizeof(PBRInfo), minBufferAlignment);
	vk::DeviceSize alphaAlignment = std::max(sizeof(AlphaInfo), minBufferAlignment);

	size_t pbrBufferSize = meshes.size() * pbrAlignment;
	this->allocator.destroyBuffer(this->pbrBuffer, this->pbrBufferAllocation);
	auto [pbrStagingBuffer, pbrStagingBufferAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, pbrBufferSize , vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuOnly });
	std::tie(this->pbrBuffer, this->pbrBufferAllocation) = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, pbrBufferSize, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly });

	size_t alphaBufferSize = (1 + alphaMaskMeshes.size()) * alphaAlignment;
	auto [alphaStagingBuffer, alphaStagingBufferAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, alphaBufferSize , vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuOnly });
	std::tie(this->alphaBuffer, this->alphaBufferAllocation) = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, alphaBufferSize, vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly });

	unsigned char* pbrSBData = reinterpret_cast<unsigned char*>(this->allocator.mapMemory(pbrStagingBufferAllocation));
	unsigned char* alphaSBData = reinterpret_cast<unsigned char*>(this->allocator.mapMemory(alphaStagingBufferAllocation));

	for (const auto& [i, mesh] : iter::enumerate(this->meshes)) {
		mesh->albedoTexture.loadToDevice(this->device, this->allocator, this->graphicsQueue, this->commandPool);
		mesh->normalTexture.loadToDevice(this->device, this->allocator, this->graphicsQueue, this->commandPool);
		mesh->metalRoughnessTexture.loadToDevice(this->device, this->allocator, this->graphicsQueue, this->commandPool);
		mesh->emissiveTexture.loadToDevice(this->device, this->allocator, this->graphicsQueue, this->commandPool);
		mesh->aoTexture.loadToDevice(this->device, this->allocator, this->graphicsQueue, this->commandPool);

		mesh->descriptorSet = this->device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ this->descriptorPool, this->pbrDescriptorSetLayout })[0];
		mesh->hasDescriptorSet = true;
		
		*reinterpret_cast<PBRInfo*>(pbrSBData + i * pbrAlignment) = mesh->materialInfo;

		std::vector<vk::DescriptorBufferInfo> pbrBufferInfos = { vk::DescriptorBufferInfo{this->pbrBuffer, i * pbrAlignment, sizeof(PBRInfo) } };
		std::vector<vk::DescriptorImageInfo> albedoImageInfos = { vk::DescriptorImageInfo{this->textureSampler, mesh->albedoTexture.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal } };
		std::vector<vk::DescriptorImageInfo> normalImageInfos = { vk::DescriptorImageInfo{this->textureSampler, mesh->normalTexture.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal } };
		std::vector<vk::DescriptorImageInfo> metallicRoughnessImageInfos = { vk::DescriptorImageInfo{this->textureSampler, mesh->metalRoughnessTexture.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal } };
		std::vector<vk::DescriptorImageInfo> aoImageInfos = { vk::DescriptorImageInfo{this->textureSampler, mesh->aoTexture.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal } };
		std::vector<vk::DescriptorImageInfo> emissiveImageInfos = { vk::DescriptorImageInfo{this->textureSampler, mesh->emissiveTexture.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal } };
		std::vector<vk::DescriptorBufferInfo> alphaBufferInfos = { vk::DescriptorBufferInfo{this->alphaBuffer, 0, sizeof(AlphaInfo) } };

		std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
			vk::WriteDescriptorSet{ mesh->descriptorSet, 0, 0, vk::DescriptorType::eUniformBuffer, {}, pbrBufferInfos},
			vk::WriteDescriptorSet{ mesh->descriptorSet, 1, 0, vk::DescriptorType::eCombinedImageSampler, albedoImageInfos },
			vk::WriteDescriptorSet{ mesh->descriptorSet, 2, 0, vk::DescriptorType::eCombinedImageSampler, normalImageInfos },
			vk::WriteDescriptorSet{ mesh->descriptorSet, 3, 0, vk::DescriptorType::eCombinedImageSampler, metallicRoughnessImageInfos },
			vk::WriteDescriptorSet{ mesh->descriptorSet, 4, 0, vk::DescriptorType::eCombinedImageSampler, aoImageInfos },
			vk::WriteDescriptorSet{ mesh->descriptorSet, 5, 0, vk::DescriptorType::eCombinedImageSampler, emissiveImageInfos },
			vk::WriteDescriptorSet{ mesh->descriptorSet, 6, 0, vk::DescriptorType::eUniformBuffer, {}, alphaBufferInfos },
		};
		this->device.updateDescriptorSets(writeDescriptorSets, {});
	}

	reinterpret_cast<AlphaInfo*>(alphaSBData)[0] = AlphaInfo{ AlphaMode::eOpaque, 0.0f };
	for (const auto& [i, mesh] : iter::enumerate(this->alphaMaskMeshes)) {
		*reinterpret_cast<AlphaInfo*>(alphaSBData + (1 + i) * alphaAlignment) = mesh->alphaInfo;

		std::vector<vk::DescriptorBufferInfo> alphaBufferInfos = { vk::DescriptorBufferInfo{this->alphaBuffer, (1 + i) * alphaAlignment, sizeof(AlphaInfo) } };
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
			vk::WriteDescriptorSet{ mesh->descriptorSet, 6, 0, vk::DescriptorType::eUniformBuffer, {}, alphaBufferInfos },
		};
		this->device.updateDescriptorSets(writeDescriptorSets, {});
	}

	this->allocator.unmapMemory(pbrStagingBufferAllocation);
	this->allocator.unmapMemory(alphaStagingBufferAllocation);

	auto cmdBuffers = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ commandPool, vk::CommandBufferLevel::ePrimary, 1 });
	vk::CommandBuffer cb = cmdBuffers[0];

	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	cb.copyBuffer(pbrStagingBuffer, this->pbrBuffer, vk::BufferCopy{0, 0, pbrBufferSize});
	cb.copyBuffer(alphaStagingBuffer, this->alphaBuffer, vk::BufferCopy{0, 0, alphaBufferSize});
	cb.end();

	vk::Fence fence = this->device.createFence(vk::FenceCreateInfo{});
	this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
	this->device.waitForFences(fence, true, UINT64_MAX);
	this->allocator.destroyBuffer(pbrStagingBuffer, pbrStagingBufferAllocation);
	this->allocator.destroyBuffer(alphaStagingBuffer, alphaStagingBufferAllocation);
	this->device.freeCommandBuffers(commandPool, cb);
	this->device.destroyFence(fence);

	this->createVertexBuffer();
}


void VulkanRenderer::start() {
	this->running = true;
	this->renderThread = std::thread([this] { this->renderLoop(); });
}


void VulkanRenderer::setPhysicalDevice(vk::PhysicalDevice physicalDevice) {
	this->physicalDevice = physicalDevice;
	const std::vector<vk::QueueFamilyProperties> queueFamilies = physicalDevice.getQueueFamilyProperties();
	for (uint32_t i = 0; i < queueFamilies.size(); i++) {
		const vk::QueueFamilyProperties& queueFamilyProperties = queueFamilies[i];
		if (queueFamilyProperties.queueFlags & vk::QueueFlagBits::eGraphics) {
			this->graphicsQueueFamilyIndex = i;
			break;
		}
	}

	physicalDevice.getSurfaceSupportKHR(this->graphicsQueueFamilyIndex, this->surface);

	if (this->device != vk::Device()) {
		this->device.destroy();
		this->allocator.destroy();
	}

	std::vector<float> queuePriorities = { 1.0f };
	vk::DeviceQueueCreateInfo queueCreateInfo = vk::DeviceQueueCreateInfo{{}, this->graphicsQueueFamilyIndex, queuePriorities};
	std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	vk::PhysicalDeviceFeatures deviceFeatures{};
	deviceFeatures.fillModeNonSolid = true;
	deviceFeatures.samplerAnisotropy = true;
	deviceFeatures.imageCubeArray = true;
	this->device = this->physicalDevice.createDevice(vk::DeviceCreateInfo{ {}, queueCreateInfo, {}, deviceExtensions, &deviceFeatures });
	this->graphicsQueue = this->device.getQueue(this->graphicsQueueFamilyIndex, 0);

	this->commandPool = this->device.createCommandPool(vk::CommandPoolCreateInfo{{vk::CommandPoolCreateFlagBits::eResetCommandBuffer}, this->graphicsQueueFamilyIndex});
	vma::AllocatorCreateInfo allocatorInfo{ {}, this->physicalDevice, this->device, };
	allocatorInfo.instance = this->vulkanInstance;
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_1;
	this->allocator = vma::createAllocator(allocatorInfo);

	std::array<vk::Format, 4> colorFormatCandidates = {
		vk::Format::eR16G16B16A16Sfloat,
		vk::Format::eR32G32B32A32Sfloat,
		vk::Format::eR8G8B8A8Srgb,
		vk::Format::eR8G8B8A8Unorm,
	};

	for (auto& format : colorFormatCandidates) {
		try {
			physicalDevice.getImageFormatProperties(format, vk::ImageType::e2D, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment, {});
			this->colorAttachmentFormat = format;
			break;
		}
		catch (vk::FormatNotSupportedError e) {
			continue;
		}
	}

	std::array<vk::Format, 4> depthFormatCandidates = {
		vk::Format::eD32Sfloat,
		vk::Format::eD24UnormS8Uint,
		vk::Format::eD16UnormS8Uint,
		vk::Format::eD16Unorm
	};

	for (auto& format : depthFormatCandidates) {
		try {
			physicalDevice.getImageFormatProperties(format, vk::ImageType::e2D, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, {});
			this->depthAttachmentFormat = format;
			break;
		}
		catch (vk::FormatNotSupportedError e) {
			continue;
		}
	}

	if (this->depthAttachmentFormat == vk::Format::eUndefined)
		throw std::runtime_error("No Supported Depth Attachment Formats Available");

	std::array<vk::Format, 3> envMapFormatCandidates = {
		vk::Format::eR16G16B16A16Sfloat,
		vk::Format::eR8G8B8A8Srgb,
		vk::Format::eR8G8B8A8Unorm,
	};

	for (auto& format : envMapFormatCandidates) {
		try {
			physicalDevice.getImageFormatProperties(format, vk::ImageType::e2D, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eSampled, {});
			this->envMapFormat = format;
			break;
		}
		catch (vk::FormatNotSupportedError e) {
			continue;
		}
	}
}

void VulkanRenderer::createSwapchain() {
	auto surfaceCap = this->physicalDevice.getSurfaceCapabilitiesKHR(this->surface);
	auto surfaceFormats = this->physicalDevice.getSurfaceFormatsKHR(this->surface);

	vk::SurfaceFormatKHR selectedFormat = surfaceFormats[0];
	if (this->settings.hdrEnabled) {
		for (auto& format : surfaceFormats) {
			if (format.colorSpace == vk::ColorSpaceKHR::eHdr10St2084EXT) {
				selectedFormat = format;
				break;
			}
			else if (format.colorSpace == vk::ColorSpaceKHR::eBt2020LinearEXT) {
				selectedFormat = format;
			}
		}
	}

	vk::SwapchainCreateInfoKHR createInfo{ {}, this->surface, std::max(surfaceCap.minImageCount, FRAMES_IN_FLIGHT), selectedFormat.format, selectedFormat.colorSpace, surfaceCap.currentExtent, 1, vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive, {}, vk::SurfaceTransformFlagBitsKHR::eIdentity, vk::CompositeAlphaFlagBitsKHR::eOpaque, this->settings.vsync ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eImmediate };
	this->swapchain = this->device.createSwapchainKHR(createInfo);
	this->swapchainExtent = surfaceCap.currentExtent;
	this->swapchainFormat = selectedFormat.format;

	for (auto& image : this->device.getSwapchainImagesKHR(this->swapchain)) {
		vk::ComponentMapping ivMapping = { vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,vk::ComponentSwizzle::eIdentity,vk::ComponentSwizzle::eIdentity };
		vk::ImageSubresourceRange ivRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		vk::ImageView iv = this->device.createImageView(vk::ImageViewCreateInfo{{}, image, vk::ImageViewType::e2D, selectedFormat.format, ivMapping, ivRange});
		this->swapchainImageViews.push_back(iv);
	}

	std::tie(this->depthImage, this->depthImageAllocation) = this->allocator.createImage(vk::ImageCreateInfo{ {}, vk::ImageType::e2D, this->depthAttachmentFormat, vk::Extent3D{this->swapchainExtent, 1}, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly });
	this->depthImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->depthImage, vk::ImageViewType::e2D, this->depthAttachmentFormat, vk::ComponentMapping{}, { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 } });
	
	std::tie(this->colorImage, this->colorImageAllocation) = this->allocator.createImage(vk::ImageCreateInfo{ {}, vk::ImageType::e2D, this->colorAttachmentFormat, vk::Extent3D{this->swapchainExtent, 1}, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly });
	this->colorImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->colorImage, vk::ImageViewType::e2D, this->colorAttachmentFormat, vk::ComponentMapping{}, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } });

	auto buffers = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, FRAMES_IN_FLIGHT });
	std::copy(buffers.begin(), buffers.end(), this->commandBuffers.begin());
}

void VulkanRenderer::createRenderPass() {

	vk::AttachmentDescription mainColorAttachment{ {}, this->colorAttachmentFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal };
	vk::AttachmentDescription mainDepthAttachment{ {}, this->depthAttachmentFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal };
	vk::AttachmentDescription presentAttachment{ {}, this->swapchainFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR };
	std::vector<vk::AttachmentDescription> attachmentDescriptions = { mainColorAttachment, mainDepthAttachment, presentAttachment };

	vk::AttachmentReference mainColorAttachmentRef{ 0, vk::ImageLayout::eColorAttachmentOptimal };
	vk::AttachmentReference mainDepthAttachmentRef{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal };
	vk::SubpassDescription mainSubpass{ {}, vk::PipelineBindPoint::eGraphics, {}, mainColorAttachmentRef, {}, &mainDepthAttachmentRef };
	
	vk::AttachmentReference tonemapColorInputAttachmentRef{ 0, vk::ImageLayout::eShaderReadOnlyOptimal };
	vk::AttachmentReference tonemapColorAttachmentRef{ 2, vk::ImageLayout::eColorAttachmentOptimal };
	vk::SubpassDescription tonemapSubpass{ {}, vk::PipelineBindPoint::eGraphics, tonemapColorInputAttachmentRef, tonemapColorAttachmentRef };

	std::vector<vk::SubpassDependency> subpassDependencies {
		vk::SubpassDependency{VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite},
		vk::SubpassDependency{VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests,vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests, vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite},
		vk::SubpassDependency{0, 1, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eInputAttachmentRead },
	};

	std::vector<vk::SubpassDescription> subpasses = { mainSubpass, tonemapSubpass };

	this->renderPass = this->device.createRenderPass(vk::RenderPassCreateInfo{{}, attachmentDescriptions, subpasses, subpassDependencies });

	this->swapchainFramebuffers.reserve(this->swapchainImageViews.size());
	for (auto& iv : this->swapchainImageViews) {
		std::vector<vk::ImageView> attachments = { this->colorImageView, this->depthImageView, iv };
		vk::Framebuffer fb = this->device.createFramebuffer(vk::FramebufferCreateInfo{{}, this->renderPass, attachments, this->swapchainExtent.width, this->swapchainExtent.height, 1});
		this->swapchainFramebuffers.push_back(fb);
	}
}

vk::ShaderModule VulkanRenderer::loadShader(const std::string& path) {
	std::vector<uint32_t> buffer;

	std::ifstream shaderfile;
	shaderfile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	shaderfile.open(path, std::ios::in | std::ios::binary);

	shaderfile.seekg(0, std::ios::end);
	size_t flen = shaderfile.tellg();
	buffer.resize(flen * sizeof(char) / sizeof(uint32_t) + (flen * sizeof(char) % sizeof(uint32_t) != 0));

	shaderfile.seekg(0, std::ios::beg);
	shaderfile.read(reinterpret_cast<char*>(buffer.data()), flen);

	vk::ShaderModule shaderModule = this->device.createShaderModule(vk::ShaderModuleCreateInfo{ {}, buffer });

	return shaderModule;
}

void VulkanRenderer::createDescriptorPool() {
	const uint32_t maxObjectCount = 512u;
	std::vector< vk::DescriptorPoolSize> poolSizes = {
		vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 4 + 5 * maxObjectCount },
		vk::DescriptorPoolSize{ vk::DescriptorType::eStorageBuffer, 1 },
		vk::DescriptorPoolSize{ vk::DescriptorType::eInputAttachment, 1},
	};
	this->descriptorPool = this->device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, maxObjectCount, poolSizes });
}

void VulkanRenderer::createPipeline() {

	vk::ShaderModule vertexModule = loadShader("./shaders/basic.vert.spv");
	vk::PipelineShaderStageCreateInfo vertexStageInfo = vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main"};

	vk::ShaderModule pbrFragModule = loadShader("./shaders/pbr.frag.spv");
	vk::PipelineShaderStageCreateInfo pbrFragStageInfo = vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, pbrFragModule, "main"};

	vk::ShaderModule solidFragModule = loadShader("./shaders/solid.frag.spv");
	vk::PipelineShaderStageCreateInfo solidFragStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eFragment, solidFragModule, "main" };

	this->shaderModules = { vertexModule, pbrFragModule, solidFragModule };

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo, pbrFragStageInfo };
	std::vector<vk::PipelineShaderStageCreateInfo> wireframeShaderStagesInfo = { vertexStageInfo, solidFragStageInfo };
	
	vk::VertexInputBindingDescription vertexBindingDescription(0, sizeof(Vertex), vk::VertexInputRate::eVertex);
	std::vector<vk::VertexInputAttributeDescription> vertexAttributeDescriptions = {
		vk::VertexInputAttributeDescription{ 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos) },
		vk::VertexInputAttributeDescription{ 1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, normal) },
		vk::VertexInputAttributeDescription{ 2, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, tangent) },
		vk::VertexInputAttributeDescription{ 3, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, bitangent) },
		vk::VertexInputAttributeDescription{ 4, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv) }
	};

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo{{}, vertexBindingDescription, vertexAttributeDescriptions};

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{{}, vk::PrimitiveTopology::eTriangleList, false};

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->swapchainExtent.width), static_cast<float>(this->swapchainExtent.height), 0.0f, 1.0f  } };
	std::vector<vk::Rect2D> scissors = { vk::Rect2D({0, 0}, this->swapchainExtent) };

	vk::PipelineViewportStateCreateInfo viewportInfo{{}, viewports, scissors};

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{ {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f };
	vk::PipelineRasterizationStateCreateInfo wireframeRasterizationInfo{ {}, false, false, vk::PolygonMode::eLine, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f };

	vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{ {}, true, true, vk::CompareOp::eLess };
	vk::PipelineDepthStencilStateCreateInfo wireframeDepthStencilInfo{ {}, false, false, vk::CompareOp::eAlways };

	vk::PipelineMultisampleStateCreateInfo multisampleInfo;

	vk::PipelineColorBlendAttachmentState opaqueColorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo opaqueColorBlendInfo{ {}, false, vk::LogicOp::eCopy, opaqueColorBlendAttachment };

	vk::PipelineColorBlendAttachmentState blendColorBlendAttachment(true, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eSubtract, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo blendColorBlendInfo{ {}, false, vk::LogicOp::eCopy, blendColorBlendAttachment };
	
	//std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };
	std::vector<vk::DynamicState> dynamicStates = { };
	vk::PipelineDynamicStateCreateInfo dynamicStateInfo{{}, dynamicStates};


	std::vector<vk::PushConstantRange> pushConstantRanges = { vk::PushConstantRange{ vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4) * 2 }, };

	std::vector<vk::DescriptorSetLayoutBinding> pbrSetBindings = {
		vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment},
		vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
		vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
		vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
		vk::DescriptorSetLayoutBinding{4, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
		vk::DescriptorSetLayoutBinding{5, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
		vk::DescriptorSetLayoutBinding{6, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment},
	};
	this->pbrDescriptorSetLayout = this->device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, pbrSetBindings });

	std::vector<vk::DescriptorSetLayoutBinding> globalDescriptorSetBindings = {  
		vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment},
		vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
		vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
		vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
		vk::DescriptorSetLayoutBinding{4, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
	};
	this->globalDescriptorSetLayout = this->device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, globalDescriptorSetBindings });

	std::vector<vk::DescriptorSetLayoutBinding> cameraDescriptorSetBindings = {
		vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment}
	};
	this->cameraDescriptorSetLayout = this->device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, cameraDescriptorSetBindings });

	std::vector<vk::DescriptorSetLayout> setLayouts = { this->globalDescriptorSetLayout, this->pbrDescriptorSetLayout, this->cameraDescriptorSetLayout };
	
	this->globalDescriptorSet = this->device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ this->descriptorPool, this->globalDescriptorSetLayout })[0];
	
	{
		std::array<vk::DescriptorSetLayout, FRAMES_IN_FLIGHT> cameraAllocateSetLayouts;
		for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++)
			cameraAllocateSetLayouts[i] = cameraDescriptorSetLayout;
		auto cameraDescriptorSets = this->device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ this->descriptorPool, cameraAllocateSetLayouts });
		std::copy(cameraDescriptorSets.begin(), cameraDescriptorSets.end(), this->cameraDescriptorSets.begin());
	}

	this->pipelineLayout = this->device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, setLayouts, pushConstantRanges });

	vk::Result r;
	std::tie(r, this->opaquePipeline) = this->device.createGraphicsPipeline(this->pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizationInfo, &multisampleInfo, &depthStencilInfo, &opaqueColorBlendInfo, &dynamicStateInfo, this->pipelineLayout, this->renderPass, 0 });
	std::tie(r, this->blendPipeline) = this->device.createGraphicsPipeline(this->pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizationInfo, &multisampleInfo, &depthStencilInfo, &blendColorBlendInfo, &dynamicStateInfo, this->pipelineLayout, this->renderPass, 0 });
	std::tie(r, this->wireframePipeline) = this->device.createGraphicsPipeline(this->pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, wireframeShaderStagesInfo, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &wireframeRasterizationInfo, &multisampleInfo, &wireframeDepthStencilInfo, &opaqueColorBlendInfo, &dynamicStateInfo, this->pipelineLayout, this->renderPass, 0 });
	
	for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		std::tie(this->cameraBuffers[i], this->cameraBufferAllocations[i]) = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, sizeof(CameraData), vk::BufferUsageFlagBits::eUniformBuffer, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuToGpu, vk::MemoryPropertyFlagBits::eHostCoherent });

		std::vector<vk::DescriptorBufferInfo> bufferInfos = { vk::DescriptorBufferInfo{this->cameraBuffers[i], 0, sizeof(CameraData)} };
		this->device.updateDescriptorSets(vk::WriteDescriptorSet{ this->cameraDescriptorSets[i], 0, 0, vk::DescriptorType::eUniformBuffer, {}, bufferInfos }, {});
	}
}


void VulkanRenderer::createTonemapPipeline() {

	vk::ShaderModule vertexModule = loadShader("./shaders/postfx.vert.spv");
	vk::PipelineShaderStageCreateInfo vertexStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main" };
	this->shaderModules.push_back(vertexModule); 

	vk::ShaderModule fragModule = loadShader("./shaders/tonemap.frag.spv");
	vk::PipelineShaderStageCreateInfo fragStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eFragment, fragModule, "main" };
	this->shaderModules.push_back(fragModule);

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo, fragStageInfo };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo{ {}, {}, {} };

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{ {}, vk::PrimitiveTopology::eTriangleList, false };

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->swapchainExtent.width), static_cast<float>(this->swapchainExtent.height), 0.0f, 1.0f  } };
	std::vector<vk::Rect2D> scissors = { vk::Rect2D({0, 0}, this->swapchainExtent) };

	vk::PipelineViewportStateCreateInfo viewportInfo{ {}, viewports, scissors };

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{ {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f };

	vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{ {}, false, false, vk::CompareOp::eAlways };

	vk::PipelineMultisampleStateCreateInfo multisampleInfo;

	vk::PipelineColorBlendAttachmentState colorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo{ {}, false, vk::LogicOp::eCopy, colorBlendAttachment };

	std::vector<vk::DynamicState> dynamicStates = {};
	vk::PipelineDynamicStateCreateInfo dynamicStateInfo{ {}, dynamicStates };


	std::vector<vk::PushConstantRange> pushConstantRanges = { vk::PushConstantRange{ vk::ShaderStageFlagBits::eFragment, 0, sizeof(float) * 2 }, };

	std::vector<vk::DescriptorSetLayoutBinding> tonemapDescriptorSetBindings = {
		vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eInputAttachment, 1, vk::ShaderStageFlagBits::eFragment},
	};
	this->tonemapDescriptorSetLayout = this->device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, tonemapDescriptorSetBindings });

	std::vector<vk::DescriptorSetLayout> setLayouts = { this->tonemapDescriptorSetLayout };
	this->tonemapPipelineLayout = this->device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, setLayouts, pushConstantRanges });

	vk::Result r;
	std::tie(r, this->tonemapPipeline) = this->device.createGraphicsPipeline(this->pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizationInfo, &multisampleInfo, &depthStencilInfo, &colorBlendInfo, &dynamicStateInfo, this->tonemapPipelineLayout, this->renderPass, 1 });
	
	this->tonemapDescriptorSet = this->device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ this->descriptorPool, this->tonemapDescriptorSetLayout })[0];

	std::vector<vk::DescriptorImageInfo> imageInfos = { vk::DescriptorImageInfo{vk::Sampler{}, this->colorImageView, vk::ImageLayout::eShaderReadOnlyOptimal } };
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet{ this->tonemapDescriptorSet, 0, 0, vk::DescriptorType::eInputAttachment, imageInfos },
	};
	this->device.updateDescriptorSets(writeDescriptorSets, {});
}

void VulkanRenderer::createVertexBuffer() {
	vk::CommandBuffer cb = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];
	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	size_t vertexBufferSize = 0u;
	size_t indexBufferSize = 0u;
	auto meshes = iter::chain(this->meshes, this->boundingBoxMeshes);
	for (auto& mesh : meshes) {
		vertexBufferSize += mesh->vertices.size() * sizeof(Vertex);
		if (mesh->isIndexed)
			indexBufferSize += mesh->indices.size() * sizeof(uint32_t);
	}

	std::tie(this->vertexIndexBuffer, this->vertexIndexBufferAllocation) = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, vertexBufferSize + indexBufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly });

	auto [stagingBuffer, SBAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, vertexBufferSize + indexBufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuOnly });

	unsigned char* stagingData = reinterpret_cast<unsigned char*>(this->allocator.mapMemory(SBAllocation));
	size_t vbOffset = 0, ibOffset = 0;
	size_t vertexCount = 0, indexCount = 0;
	for (auto& mesh : meshes) {
		size_t len = mesh->vertices.size() * sizeof(Vertex);
		memcpy(stagingData + vbOffset, mesh->vertices.data(), len);
		vbOffset += len;

		if (mesh->isIndexed) {
			len = mesh->indices.size() * sizeof(uint32_t);
			for (auto [i, index] : iter::enumerate(mesh->indices)) {
				*(reinterpret_cast<uint32_t*>(stagingData + vertexBufferSize + ibOffset) + i) = index + vertexCount;
			}
			ibOffset += len;
		}
		
		mesh->firstIndex = indexCount;
		mesh->firstVertex = vertexCount;
		indexCount += mesh->indices.size();
		vertexCount += mesh->vertices.size();
	}
	this->allocator.unmapMemory(SBAllocation);

	cb.copyBuffer(stagingBuffer, this->vertexIndexBuffer, vk::BufferCopy{0, 0, vertexBufferSize + indexBufferSize});
	this->vertexBufferOffset = 0u;
	this->indexBufferOffset = vertexBufferSize;

	cb.end();

	vk::Fence fence = this->device.createFence(vk::FenceCreateInfo{});
	this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
	this->device.waitForFences(fence, true, UINT64_MAX);
	this->allocator.destroyBuffer(stagingBuffer, SBAllocation);
	this->device.freeCommandBuffers(this->commandPool, cb);
	this->device.destroyFence(fence);
}

void VulkanRenderer::destroyVertexBuffer() {
	this->allocator.destroyBuffer(this->vertexIndexBuffer, this->vertexIndexBufferAllocation);
}


//Source: http://www.cs.otago.ac.nz/postgrads/alexis/planeExtraction.pdf
std::array<glm::vec4, 6> frustumPlanesFromMatrix(const glm::mat4& M) {
	std::array<glm::vec4, 6> planes;/*{
		glm::vec4{ 1.0f, 0.0f, 0.0f, 1.0f },
		glm::vec4{ -1.0f, 0.0f, 0.0f, 1.0f },
		glm::vec4{ 0.0f, 1.0f, 0.0f, 1.0f },
		glm::vec4{ 0.0f, -1.0f, 0.0f, 1.0f },
		glm::vec4{ 0.0f, 0.0f, 1.0f, 1.0f },
		glm::vec4{ 0.0f, 0.0f, -1.0f, 0.0f },
	};*/

	//-X
	planes[0].x = M[0][3] + M[0][0];
	planes[0].y = M[1][3] + M[1][0];
	planes[0].z = M[2][3] + M[2][0];
	planes[0].w = M[3][3] + M[3][0];
	//+X
	planes[1].x = M[0][3] - M[0][0];
	planes[1].y = M[1][3] - M[1][0];
	planes[1].z = M[2][3] - M[2][0];
	planes[1].w = M[3][3] - M[3][0];
	//-Y
	planes[2].x = M[0][3] + M[0][1];
	planes[2].y = M[1][3] + M[1][1];
	planes[2].z = M[2][3] + M[2][1];
	planes[2].w = M[3][3] + M[3][1];
	//+Y
	planes[3].x = M[0][3] - M[0][1];
	planes[3].y = M[1][3] - M[1][1];
	planes[3].z = M[2][3] - M[2][1];
	planes[3].w = M[3][3] - M[3][1];
	//+Z
	planes[4].x = M[0][3] - M[0][2];
	planes[4].y = M[1][3] - M[1][2];
	planes[4].z = M[2][3] - M[2][2];
	planes[4].w = M[3][3] - M[3][2];
	//-Z
	planes[5].x = M[0][2];
	planes[5].y = M[1][2];
	planes[5].z = M[2][2];
	planes[5].w = M[3][2];

	return planes;
}

bool cullMesh(const Mesh& mesh, const glm::mat4& modelviewproj) {
	const auto& planes = frustumPlanesFromMatrix(modelviewproj); //planes pointing inside frustum

	for (auto& plane : planes) {
		glm::vec3 nVertex = mesh.boundingBox.first;

		if (plane.x >= 0.0f) nVertex.x = mesh.boundingBox.second.x;
		if (plane.y >= 0.0f) nVertex.y = mesh.boundingBox.second.y;
		if (plane.z >= 0.0f) nVertex.z = mesh.boundingBox.second.z;

		if (glm::dot(nVertex, glm::vec3(plane)) + plane.w < 0.0f) //negative halfspace (plane equation)
			return true;
	}
	return false;
}

void VulkanRenderer::drawMeshes(const std::vector<std::shared_ptr<Mesh>>& meshes, const vk::CommandBuffer& cb, uint32_t frameIndex, const glm::mat4& viewproj, const glm::vec3& cameraPos, bool frustumCull, MeshSortingMode sortingMode) {

	auto culledMeshes = iter::filter([viewproj](const std::shared_ptr<Mesh> mesh) {
		const glm::mat4 model = mesh->node->modelMatrix();
		const glm::mat4 modelviewproj = viewproj * model;
		return !cullMesh(*mesh, modelviewproj);
		}, meshes);

	std::vector<std::shared_ptr<Mesh>> sortedMeshes;
	switch (sortingMode) {
	case MeshSortingMode::eFrontToBack:
		for (const auto& el : iter::sorted(culledMeshes, [&cameraPos](const std::shared_ptr<Mesh>& a, const std::shared_ptr<Mesh>& b) { return glm::distance(a->barycenter(), cameraPos) < glm::distance(b->barycenter(), cameraPos); }))
			sortedMeshes.push_back(el);
		break;
	case MeshSortingMode::eBackToFront:
		for (const auto& el : iter::sorted(culledMeshes, [&cameraPos](const std::shared_ptr<Mesh>& a, const std::shared_ptr<Mesh>& b) { return glm::distance(a->barycenter(), cameraPos) > glm::distance(b->barycenter(), cameraPos); }))
			sortedMeshes.push_back(el);
		break;
	default:
		for (const auto& el : culledMeshes)
			sortedMeshes.push_back(el);
		break;
	}

	for (auto& mesh : sortedMeshes) {
		std::vector descriptorSets = { this->globalDescriptorSet };
		if (mesh->hasDescriptorSet)
			descriptorSets.push_back(mesh->descriptorSet);
		descriptorSets.push_back(this->cameraDescriptorSets[frameIndex]);

		cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, this->pipelineLayout, 0, descriptorSets, {});

		const glm::mat4 model = mesh->node->modelMatrix();
		const glm::mat4 modelviewproj = viewproj * model;

		std::vector<glm::mat4> pushConstants = { modelviewproj, model };

		cb.pushConstants<glm::mat4x4>(this->pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstants);

		if (mesh->isIndexed)
			cb.drawIndexed(mesh->indices.size(), 1, mesh->firstIndex, 0, 0);
		else
			cb.draw(mesh->vertices.size(), 1, mesh->firstVertex, 0);
	}
}

void VulkanRenderer::renderLoop() {
	const std::vector<vk::ClearValue> clearValues = { vk::ClearColorValue(std::array<float, 4>({0.0f, 0.0f, 0.0f, 1.0f})), vk::ClearDepthStencilValue{1.0f}, vk::ClearColorValue(std::array<float, 4>({0.0f, 0.0f, 0.0f, 1.0f})) };

	size_t frameIndex = 0;
	auto frameTime = std::chrono::high_resolution_clock::now();
	double runningTime = 0;

	glm::mat4 proj = glm::scale(glm::vec3{ 1.0f, -1.0f, 1.0f }) * glm::perspective(glm::radians(35.0f), this->swapchainExtent.width / (float)this->swapchainExtent.height, 0.01f, 1000.0f);

	while (this->running) {
		frameIndex = (frameIndex + 1) % FRAMES_IN_FLIGHT;

		this->device.waitForFences(this->frameFences[frameIndex], true, UINT64_MAX);
		this->device.resetFences(this->frameFences[frameIndex]);

		auto newFrameTime = std::chrono::high_resolution_clock::now();
		double deltaTime = std::chrono::duration<double>(newFrameTime - frameTime).count();
		runningTime += deltaTime;
		frameTime = newFrameTime;

		for (const auto& n : this->rootNodes) {
			n->setAnimationTime(runningTime);
			n->recalculateModel(glm::mat4{ 1.0f });
		}

		this->renderShadowMaps(frameIndex);

		glm::vec3 cameraPos;
		glm::mat4 view;
		std::tie(cameraPos, view) = this->_camera.positionAndMatrix();

		glm::mat4 viewproj = proj * view;
		glm::mat4 invviewproj = glm::inverse(viewproj);

		CameraData* cameraUBO = reinterpret_cast<CameraData*>(this->allocator.mapMemory(this->cameraBufferAllocations[frameIndex]));
		*cameraUBO = CameraData{ glm::vec4{cameraPos, 1.0}, view, viewproj, invviewproj };
		this->allocator.unmapMemory(this->cameraBufferAllocations[frameIndex]);

		auto&& [r, imageIndex] = this->device.acquireNextImageKHR(this->swapchain, UINT64_MAX, this->imageAcquiredSemaphores[frameIndex]);

		vk::CommandBuffer& cb = this->commandBuffers[imageIndex];
		cb.reset();
		cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		cb.bindVertexBuffers(0, this->vertexIndexBuffer, { 0 });
		cb.bindIndexBuffer(this->vertexIndexBuffer, this->indexBufferOffset, vk::IndexType::eUint32);
		cb.beginRenderPass(vk::RenderPassBeginInfo{ this->renderPass, this->swapchainFramebuffers[imageIndex], vk::Rect2D({ 0, 0 }, this->swapchainExtent), clearValues }, vk::SubpassContents::eInline);

		if (!this->opaqueMeshes.empty()) {
			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->opaquePipeline);
			this->drawMeshes(this->opaqueMeshes, cb, frameIndex, viewproj, cameraPos, true, MeshSortingMode::eFrontToBack);
		}

		std::vector<vk::DescriptorSet> envDescriptorSets = { this->globalDescriptorSet, this->cameraDescriptorSets[frameIndex] };
		cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, this->envPipelineLayout, 0, envDescriptorSets, {});
		cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->envPipeline);
		cb.draw(6, 1, 0, 0);

		if (!this->nonOpaqueMeshes.empty()) {
			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->blendPipeline);
			this->drawMeshes(this->nonOpaqueMeshes, cb, frameIndex, viewproj, cameraPos, true, MeshSortingMode::eBackToFront);
		}

		/*if (!this->boundingBoxMeshes.empty()) {
			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->wireframePipeline);
			for (auto& mesh : this->boundingBoxMeshes) {
				this->drawMesh(cb, *mesh, viewproj, cameraPos, false);
			}
		}*/


		cb.nextSubpass(vk::SubpassContents::eInline);
		cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->tonemapPipeline);
		cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, this->tonemapPipelineLayout, 0, this->tonemapDescriptorSet, {});
		cb.pushConstants<float>(this->tonemapPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, { 1.0f, 2.2f });
		cb.draw(6, 1, 0, 0);

		cb.endRenderPass();
		cb.end();

		std::array<vk::Semaphore, 2> awaitSemaphores = { this->imageAcquiredSemaphores[frameIndex], this->shadowPassFinishedSemaphores[frameIndex] };
		std::array<vk::PipelineStageFlags, 2> waitStageFlags = { vk::PipelineStageFlagBits::eEarlyFragmentTests, vk::PipelineStageFlagBits::eFragmentShader };
		this->graphicsQueue.submit(vk::SubmitInfo{ awaitSemaphores, waitStageFlags, this->commandBuffers[imageIndex], this->renderFinishedSemaphores[frameIndex] }, this->frameFences[frameIndex]);
		this->graphicsQueue.presentKHR(vk::PresentInfoKHR(this->renderFinishedSemaphores[frameIndex], this->swapchain, imageIndex));

	}
}