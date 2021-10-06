#include <fstream>
#include <chrono>

#include <itertools/enumerate.hpp>

#include "VulkanRenderer.h"

#include <glm/gtx/normal.hpp>

#include "Vertex.h"
#include "Mesh.h"
#include "PointLight.h"

const int FRAMES_IN_FLIGHT = 2;

std::vector<uint32_t> readShaderFile(std::string&& path) {
	std::vector<uint32_t> buffer;

	std::ifstream shaderfile;
	shaderfile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	shaderfile.open(path, std::ios::in | std::ios::binary);

	shaderfile.seekg(0, std::ios::end);
	size_t flen = shaderfile.tellg();
	buffer.resize(flen * sizeof(char) / sizeof(uint32_t) + (flen * sizeof(char) % sizeof(uint32_t) != 0));

	shaderfile.seekg(0, std::ios::beg);
	shaderfile.read(reinterpret_cast<char*>(buffer.data()), flen);

	return buffer;
}

VulkanRenderer::VulkanRenderer(vkfw::Window window) : window(window) {

	vk::ApplicationInfo appInfo{"Custom Vulkan Renderer", 1};
#ifdef _DEBUG
	const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
#else 
	const std::vector<const char*> validationLayers = { };
#endif
	std::vector<const char*> vkfwExtensions;
	{
		uint32_t c;
		const char** v = vkfw::getRequiredInstanceExtensions(&c);
		vkfwExtensions.resize(c);
		memcpy(vkfwExtensions.data(), v, sizeof(char**) * c);
	}
	const std::vector<const char*> vulkanExtensions = vkfwExtensions;

	this->vulkanInstance = vk::createInstance(vk::InstanceCreateInfo{{}, &appInfo, validationLayers, vulkanExtensions});
	this->surface = vkfw::createWindowSurface(this->vulkanInstance, this->window);

	std::vector<vk::PhysicalDevice> availableDevices = this->vulkanInstance.enumeratePhysicalDevices();
	if (availableDevices.empty()) throw std::runtime_error("No devices with Vulkan support are available");
	this->setPhysicalDevice(availableDevices.front());
	
	this->textureSampler = this->device.createSampler(vk::SamplerCreateInfo{ {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, 0.0f, true, this->physicalDevice.getProperties().limits.maxSamplerAnisotropy, false, vk::CompareOp::eNever, 0.0f, VK_LOD_CLAMP_NONE});
	
	this->BRDFLutTexture = Texture("./textures/ibl_brdf_lut.png", 1);
	this->BRDFLutTexture.loadToDevice(this->device, this->allocator, this->graphicsQueue, this->commandPool);

	this->createSwapchain();
	this->createRenderPass();
	this->createPipeline();
	this->createEnvPipeline();

	this->acquireImageSemaphores.reserve(FRAMES_IN_FLIGHT);
	this->presentSemaphores.reserve(FRAMES_IN_FLIGHT);
	this->frameFences.reserve(FRAMES_IN_FLIGHT);
	for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		this->acquireImageSemaphores.push_back(this->device.createSemaphore(vk::SemaphoreCreateInfo{}));
		this->presentSemaphores.push_back(this->device.createSemaphore(vk::SemaphoreCreateInfo{}));
		this->frameFences.push_back(this->device.createFence(vk::FenceCreateInfo{}));
	}
}

VulkanRenderer::~VulkanRenderer() {
	this->running = false;

	for (auto& sm : this->shaderModules) {
		this->device.destroyShaderModule(sm);
	}
	this->shaderModules.clear();

	this->device.destroyPipelineLayout(this->pipelineLayout);

	for (auto& fb : this->swapchainFramebuffers) {
		this->device.destroyFramebuffer(fb);
	}
	this->swapchainFramebuffers.clear();

	this->device.destroyRenderPass(this->renderPass);

	for (auto& iv : this->swapchainImageViews) {
		this->device.destroyImageView(iv);
	}
	this->swapchainImageViews.clear();
	
	this->destroyVertexBuffer();

	this->device.destroyPipeline(this->opaquePipeline);
	this->device.destroyPipeline(this->blendPipeline);
	this->device.destroySwapchainKHR(this->swapchain);
	this->device.destroyCommandPool(this->commandPool);
	for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		this->device.destroySemaphore(this->acquireImageSemaphores[i]);
		this->device.destroySemaphore(this->presentSemaphores[i]);
		this->device.destroyFence(this->frameFences[i]);
	}
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

void VulkanRenderer::stageTexture(vk::Image image, TextureInfo& textureInfo) {

	auto [stagingBuffer, sbAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {},  textureInfo.data.size() * sizeof(byte), vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuOnly });

	void* sbData = this->allocator.mapMemory(sbAllocation);
	memcpy(sbData, textureInfo.data.data(), textureInfo.data.size() * sizeof(byte));
	this->allocator.unmapMemory(sbAllocation);

	auto cmdBuffers = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, 1 });
	vk::CommandBuffer cb = cmdBuffers[0];
	
	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{ {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
	std::vector<vk::BufferImageCopy> copyRegions = { vk::BufferImageCopy{0, 0, 0, vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0}, {textureInfo.width, textureInfo.height, 1}  } };
	cb.copyBufferToImage(stagingBuffer, image, vk::ImageLayout::eTransferDstOptimal, copyRegions);
	cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
	cb.end();

	vk::Fence fence = this->device.createFence(vk::FenceCreateInfo{});
	this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
	this->device.waitForFences(fence, true, UINT64_MAX);
	this->allocator.destroyBuffer(stagingBuffer, sbAllocation);
	this->device.freeCommandBuffers(this->commandPool, cb);
	this->device.destroyFence(fence);
}

void VulkanRenderer::setEnvironmentMap(const std::array<TextureInfo, 6>& textureInfos) {
	std::tie(this->envMapImage, this->envMapAllocation) = this->allocator.createImage(
		vk::ImageCreateInfo{ {vk::ImageCreateFlagBits::eCubeCompatible}, vk::ImageType::e2D, vk::Format::eR32G32B32A32Sfloat, vk::Extent3D{textureInfos[0].width, textureInfos[0].height, 1}, 1, 6, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	this->envMapImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->envMapImage, vk::ImageViewType::eCube, vk::Format::eR32G32B32A32Sfloat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 } });

	auto [stagingBuffer, sbAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {},  textureInfos[0].data.size() * sizeof(byte), vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuOnly });

	void* sbData = this->allocator.mapMemory(sbAllocation);

	vk::CommandBuffer cb = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];
	vk::Fence fence = this->device.createFence(vk::FenceCreateInfo{});

	for (auto&& [i, textureInfo] : iter::enumerate(textureInfos)) {
		memcpy(sbData, textureInfo.data.data(), textureInfo.data.size() * sizeof(byte));
		
		cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{ {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->envMapImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, static_cast<uint32_t>(i), 1} });
		std::vector<vk::BufferImageCopy> copyRegions = { vk::BufferImageCopy{0, 0, 0, vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, static_cast<uint32_t>(i), 1}, {0, 0, 0}, {textureInfo.width, textureInfo.height, 1}  } };
		cb.copyBufferToImage(stagingBuffer, this->envMapImage, vk::ImageLayout::eTransferDstOptimal, copyRegions);
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->envMapImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, static_cast<uint32_t>(i), 1} });
		cb.end();

		this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
		this->device.waitForFences(fence, true, UINT64_MAX);
		this->device.resetFences(fence);
	}

	this->allocator.unmapMemory(sbAllocation);
	this->allocator.destroyBuffer(stagingBuffer, sbAllocation);
	this->device.destroyFence(fence);
	this->device.freeCommandBuffers(this->commandPool, cb);

	vk::DescriptorImageInfo envMapImageInfo{this->textureSampler, this->envMapImageView, vk::ImageLayout::eShaderReadOnlyOptimal };

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet{ this->globalDescriptorSet, 1, 0, vk::DescriptorType::eCombinedImageSampler, envMapImageInfo },
	};
	this->device.updateDescriptorSets(writeDescriptorSets, {});

	this->makeDiffuseEnvMap();
	this->makeSpecularEnvMap();
}

std::tuple<vk::Pipeline, vk::PipelineLayout> VulkanRenderer::createEnvMapDiffuseBakePipeline(vk::RenderPass renderPass) {
	std::vector<uint32_t> vertexBytecode = readShaderFile("./shaders/envbake.vert.spv");
	vk::ShaderModule vertexModule = this->device.createShaderModule(vk::ShaderModuleCreateInfo{ {}, vertexBytecode });
	vk::PipelineShaderStageCreateInfo vertexStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main" };
	this->shaderModules.push_back(vertexModule);

	std::vector<uint32_t> fragBytecode = readShaderFile("./shaders/envbakediffuse.frag.spv");
	vk::ShaderModule fragModule = this->device.createShaderModule(vk::ShaderModuleCreateInfo{ {}, fragBytecode });
	vk::PipelineShaderStageCreateInfo fragStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eFragment, fragModule, "main" };
	this->shaderModules.push_back(fragModule);

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo, fragStageInfo };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo{ {}, {}, {} };

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{ {}, vk::PrimitiveTopology::eTriangleList, false };

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->envMapDiffuseResolution), static_cast<float>(this->envMapDiffuseResolution), 0.0f, 0.1f } };
	std::vector<vk::Rect2D> scissors = { vk::Rect2D({0, 0}, vk::Extent2D{this->envMapDiffuseResolution, this->envMapDiffuseResolution}) };

	vk::PipelineViewportStateCreateInfo viewportInfo{ {}, viewports, scissors };

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{ {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f };

	vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{ {}, false, false, vk::CompareOp::eAlways };

	vk::PipelineMultisampleStateCreateInfo multisampleInfo;

	vk::PipelineColorBlendAttachmentState colorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo{ {}, false, vk::LogicOp::eCopy, colorBlendAttachment };

	vk::PipelineDynamicStateCreateInfo dynamicStateInfo{ {}, {} };

	std::vector<vk::PushConstantRange> pushConstantRanges = { vk::PushConstantRange{ vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4) * 1 }, };

	std::vector<vk::DescriptorSetLayout> setLayouts = { this->globalDescriptorSetLayout };

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ {}, setLayouts, pushConstantRanges };

	vk::PipelineLayout pipelineLayout = this->device.createPipelineLayout(pipelineLayoutInfo);

	auto [r, pipeline] = this->device.createGraphicsPipeline(vk::PipelineCache(), vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizationInfo, &multisampleInfo, &depthStencilInfo, &colorBlendInfo, &dynamicStateInfo, pipelineLayout, renderPass, 0 });
	
	return { pipeline, pipelineLayout };
}

std::tuple<vk::RenderPass, std::array<vk::ImageView, 6>, std::array<vk::Framebuffer, 6>> VulkanRenderer::createEnvMapDiffuseBakeRenderPass() {
	vk::AttachmentDescription colorAttachment{ {}, vk::Format::eR32G32B32A32Sfloat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal };
	std::vector<vk::AttachmentDescription> attachmentDescriptions = { colorAttachment };

	vk::AttachmentReference colorAttachmentRef{ 0, vk::ImageLayout::eColorAttachmentOptimal };
	vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRef, {});

	vk::SubpassDependency subpassDependency{ VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite };

	vk::RenderPass renderPass = this->device.createRenderPass(vk::RenderPassCreateInfo{ {}, attachmentDescriptions, subpass, subpassDependency });

	std::array<vk::Framebuffer, 6> framebuffers;
	std::array<vk::ImageView, 6> imageViews;
	for (unsigned short i = 0; i < 6; i++) {
		vk::ImageView iv = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->envMapDiffuseImage, vk::ImageViewType::e2D, vk::Format::eR32G32B32A32Sfloat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, i, 1} });
		imageViews[i] = iv;
		std::vector<vk::ImageView> attachments = { iv };
		vk::Framebuffer fb = this->device.createFramebuffer(vk::FramebufferCreateInfo{ {}, renderPass, attachments, this->envMapDiffuseResolution, this->envMapDiffuseResolution, 1 });
		framebuffers[i] = fb;
	}

	return { renderPass, imageViews, framebuffers };
}

void VulkanRenderer::makeDiffuseEnvMap() {
	std::tie(this->envMapDiffuseImage, this->envMapDiffuseAllocation) = this->allocator.createImage(
		vk::ImageCreateInfo{ {vk::ImageCreateFlagBits::eCubeCompatible}, vk::ImageType::e2D, vk::Format::eR32G32B32A32Sfloat, vk::Extent3D{this->envMapDiffuseResolution, this->envMapDiffuseResolution, 1}, 1, 6, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	this->envMapDiffuseImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->envMapDiffuseImage, vk::ImageViewType::eCube, vk::Format::eR32G32B32A32Sfloat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 } });
	
	auto&& [renderPass, imageViews, framebuffers] = this->createEnvMapDiffuseBakeRenderPass();
	auto&& [pipeline, pipelineLayout] = this->createEnvMapDiffuseBakePipeline(renderPass);

	vk::CommandBuffer cb = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];
	vk::Fence fence = this->device.createFence(vk::FenceCreateInfo{});
	
	const std::vector<vk::ClearValue> clearValues = { vk::ClearColorValue(std::array<float, 4>({0.0f, 0.0f, 0.0f, 1.0f})) };
	
	glm::mat4 proj = glm::scale(glm::vec3{ 1.0f, -1.0f, 1.0f });
	std::array<glm::mat4, 6> views = {
		glm::rotate(glm::radians(-90.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
		glm::rotate(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
		glm::rotate(glm::radians(90.0f), glm::vec3{1.0f, 0.0f, 0.0f}),
		glm::rotate(glm::radians(-90.0f), glm::vec3{1.0f, 0.0f, 0.0f}),
		glm::mat4{1.0f},
		glm::rotate(glm::radians(180.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
	};
	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	for (unsigned short i = 0; i < 6; i++) {
		cb.beginRenderPass(vk::RenderPassBeginInfo{ renderPass, framebuffers[i], vk::Rect2D{{0, 0}, {this->envMapDiffuseResolution, this->envMapDiffuseResolution}}, clearValues }, vk::SubpassContents::eInline);
		cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, this->globalDescriptorSet, {});
		cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
		glm::mat4 view = views[i];
		cb.pushConstants<glm::mat4>(pipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, glm::inverse(proj * view));
		cb.draw(6, 1, 0, 0);
		cb.endRenderPass();
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->envMapDiffuseImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, i, 1} });
	}
	cb.end();
	this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
	this->device.waitForFences(fence, true, UINT64_MAX);

	this->device.destroyFence(fence);
	this->device.freeCommandBuffers(this->commandPool, cb);
	this->device.destroyPipeline(pipeline);
	this->device.destroyPipelineLayout(pipelineLayout);
	for (unsigned short i = 0; i < 6; i++) {
		this->device.destroyFramebuffer(framebuffers[i]);
		this->device.destroyImageView(imageViews[i]);
	}
	this->device.destroyRenderPass(renderPass);


	std::vector<vk::DescriptorImageInfo> imageInfos = { vk::DescriptorImageInfo{this->textureSampler, this->envMapDiffuseImageView, vk::ImageLayout::eShaderReadOnlyOptimal } };

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet{ this->globalDescriptorSet, 2, 0, vk::DescriptorType::eCombinedImageSampler, imageInfos },
	};
	this->device.updateDescriptorSets(writeDescriptorSets, {});
}


std::tuple<vk::Pipeline, vk::PipelineLayout> VulkanRenderer::createEnvMapSpecularBakePipeline(vk::RenderPass renderPass) {
	std::vector<uint32_t> vertexBytecode = readShaderFile("./shaders/envbake.vert.spv");
	vk::ShaderModule vertexModule = this->device.createShaderModule(vk::ShaderModuleCreateInfo{ {}, vertexBytecode });
	vk::PipelineShaderStageCreateInfo vertexStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main" };
	this->shaderModules.push_back(vertexModule);

	std::vector<uint32_t> fragBytecode = readShaderFile("./shaders/envbakespecular.frag.spv");
	vk::ShaderModule fragModule = this->device.createShaderModule(vk::ShaderModuleCreateInfo{ {}, fragBytecode });
	vk::PipelineShaderStageCreateInfo fragStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eFragment, fragModule, "main" };
	this->shaderModules.push_back(fragModule);

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo, fragStageInfo };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo{ {}, {}, {} };

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{ {}, vk::PrimitiveTopology::eTriangleList, false };

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->envMapDiffuseResolution), static_cast<float>(this->envMapDiffuseResolution), 0.0f, 0.1f } };
	std::vector<vk::Rect2D> scissors = { vk::Rect2D({0, 0}, vk::Extent2D{this->envMapDiffuseResolution, this->envMapDiffuseResolution}) };

	vk::PipelineViewportStateCreateInfo viewportInfo{ {}, viewports, scissors };

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{ {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f };

	vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{ {}, false, false, vk::CompareOp::eAlways };

	vk::PipelineMultisampleStateCreateInfo multisampleInfo;

	vk::PipelineColorBlendAttachmentState colorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo{ {}, false, vk::LogicOp::eCopy, colorBlendAttachment };

	std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	vk::PipelineDynamicStateCreateInfo dynamicStateInfo{ {}, dynamicStates };

	std::vector<vk::PushConstantRange> pushConstantRanges = { vk::PushConstantRange{ vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(glm::mat4) * 1 + sizeof(float) }, };

	std::vector<vk::DescriptorSetLayout> setLayouts = { this->globalDescriptorSetLayout };

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ {}, setLayouts, pushConstantRanges };

	vk::PipelineLayout pipelineLayout = this->device.createPipelineLayout(pipelineLayoutInfo);

	auto [r, pipeline] = this->device.createGraphicsPipeline(vk::PipelineCache(), vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizationInfo, &multisampleInfo, &depthStencilInfo, &colorBlendInfo, &dynamicStateInfo, pipelineLayout, renderPass, 0 });

	return { pipeline, pipelineLayout };
}

std::tuple<vk::RenderPass, std::array<std::array<vk::ImageView, 10>, 6>, std::array<std::array<vk::Framebuffer, 10>, 6>> VulkanRenderer::createEnvMapSpecularBakeRenderPass() {
	vk::AttachmentDescription colorAttachment{ {}, vk::Format::eR32G32B32A32Sfloat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal };
	std::vector<vk::AttachmentDescription> attachmentDescriptions = { colorAttachment };

	vk::AttachmentReference colorAttachmentRef{ 0, vk::ImageLayout::eColorAttachmentOptimal };
	vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRef, {});

	vk::SubpassDependency subpassDependency{ VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite };

	vk::RenderPass renderPass = this->device.createRenderPass(vk::RenderPassCreateInfo{ {}, attachmentDescriptions, subpass, subpassDependency });

	std::array <std::array<vk::Framebuffer,10>, 6> framebuffers;
	std::array<std::array<vk::ImageView, 10>, 6> imageViews;
	for (unsigned short face = 0; face < 6; face++) {
		for (unsigned short roughnessLevel = 0; roughnessLevel < 10; roughnessLevel++) {
			vk::ImageView iv = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->envMapSpecularImage, vk::ImageViewType::e2D, vk::Format::eR32G32B32A32Sfloat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, roughnessLevel, 1, face, 1} });
			imageViews[face][roughnessLevel] = iv;
			std::vector<vk::ImageView> attachments = { iv };
			uint32_t fbRes = this->envMapSpecularResolution / (1 << roughnessLevel);
			vk::Framebuffer fb = this->device.createFramebuffer(vk::FramebufferCreateInfo{ {}, renderPass, attachments, fbRes, fbRes, 1 });
			framebuffers[face][roughnessLevel] = fb;
		}
	}

	return { renderPass, imageViews, framebuffers };
}

void VulkanRenderer::makeSpecularEnvMap() {
	std::tie(this->envMapSpecularImage, this->envMapSpecularAllocation) = this->allocator.createImage(
		vk::ImageCreateInfo{ {vk::ImageCreateFlagBits::eCubeCompatible}, vk::ImageType::e2D, vk::Format::eR32G32B32A32Sfloat, vk::Extent3D{this->envMapSpecularResolution, this->envMapSpecularResolution, 1}, 10, 6, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	this->envMapSpecularImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->envMapSpecularImage, vk::ImageViewType::eCube, vk::Format::eR32G32B32A32Sfloat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 10, 0, 6 } });

	auto&& [renderPass, imageViews, framebuffers] = this->createEnvMapSpecularBakeRenderPass();
	auto&& [pipeline, pipelineLayout] = this->createEnvMapSpecularBakePipeline(renderPass);

	vk::CommandBuffer cb = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];
	vk::Fence fence = this->device.createFence(vk::FenceCreateInfo{});

	const std::vector<vk::ClearValue> clearValues = { vk::ClearColorValue(std::array<float, 4>({0.0f, 0.0f, 0.0f, 1.0f})) };

	glm::mat4 proj = glm::scale(glm::vec3{ 1.0f, -1.0f, 1.0f });
	std::array<glm::mat4, 6> views = {
		glm::rotate(glm::radians(-90.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
		glm::rotate(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
		glm::rotate(glm::radians(90.0f), glm::vec3{1.0f, 0.0f, 0.0f}),
		glm::rotate(glm::radians(-90.0f), glm::vec3{1.0f, 0.0f, 0.0f}),
		glm::mat4{1.0f},
		glm::rotate(glm::radians(180.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
	};
	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	for (unsigned short face = 0; face < 6; face++) {
		for (unsigned short roughnessLevel = 0; roughnessLevel < 10; roughnessLevel++) {
			uint32_t renderRes = this->envMapSpecularResolution / (1 << roughnessLevel);
			cb.beginRenderPass(vk::RenderPassBeginInfo{ renderPass, framebuffers[face][roughnessLevel], vk::Rect2D{{0, 0}, {renderRes, renderRes}}, clearValues }, vk::SubpassContents::eInline);
			cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, this->globalDescriptorSet, {});
			cb.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
			cb.setViewport(0, vk::Viewport{ 0.0f, 0.0f, static_cast<float>(renderRes), static_cast<float>(renderRes) });
			cb.setScissor(0, vk::Rect2D{ {0, 0}, {renderRes, renderRes} });
			glm::mat4 view = views[face];
			cb.pushConstants<glm::mat4>(pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, glm::inverse(proj * view));
			cb.pushConstants<float>(pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4), static_cast<float>(roughnessLevel)/9);
			cb.draw(6, 1, 0, 0);
			cb.endRenderPass();
			cb.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->envMapSpecularImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, roughnessLevel, 1, face, 1} });
		}
	}
	cb.end();
	this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
	this->device.waitForFences(fence, true, UINT64_MAX);

	this->device.destroyFence(fence);
	this->device.freeCommandBuffers(this->commandPool, cb);
	this->device.destroyPipeline(pipeline);
	this->device.destroyPipelineLayout(pipelineLayout);
	for (unsigned short face = 0; face < 6; face++) {
		for (unsigned short roughnessLevel = 0; roughnessLevel < 10; roughnessLevel++) {
			this->device.destroyFramebuffer(framebuffers[face][roughnessLevel]);
			this->device.destroyImageView(imageViews[face][roughnessLevel]);
		}
	}
	this->device.destroyRenderPass(renderPass);


	vk::DescriptorImageInfo envMapSpecularImageInfo {this->textureSampler, this->envMapSpecularImageView, vk::ImageLayout::eShaderReadOnlyOptimal };
	vk::DescriptorImageInfo brdfLUTImageInfo{ this->textureSampler, this->BRDFLutTexture.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal };

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet{ this->globalDescriptorSet, 1, 0, vk::DescriptorType::eCombinedImageSampler, envMapSpecularImageInfo },
		vk::WriteDescriptorSet{ this->globalDescriptorSet, 3, 0, vk::DescriptorType::eCombinedImageSampler, brdfLUTImageInfo },
	};
	this->device.updateDescriptorSets(writeDescriptorSets, {});
}

void VulkanRenderer::setLights(std::vector<PointLight> lights) {
	if (!lights.size())
		return;

	size_t bufferSize = lights.size() * sizeof(PointLight);
	this->lightsBuffer = this->device.createBuffer(vk::BufferCreateInfo{ {}, bufferSize, vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive });

	auto memRequirements = this->device.getBufferMemoryRequirements(this->lightsBuffer);

	auto memProperties = this->physicalDevice.getMemoryProperties();

	uint32_t selectedMemoryTypeIndex = 0;
	for (auto&& [i, memoryType] : iter::enumerate(memProperties.memoryTypes)) {
		if (memoryType.propertyFlags & (vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible)) {
			selectedMemoryTypeIndex = i;
			break;
		}
	}

	this->lightsBufferMemory = this->device.allocateMemory(vk::MemoryAllocateInfo{ memRequirements.size , selectedMemoryTypeIndex });
	this->device.bindBufferMemory(this->lightsBuffer, this->lightsBufferMemory, 0);

	void* data = this->device.mapMemory(this->lightsBufferMemory, 0, bufferSize);
	memcpy(data, lights.data(), lights.size() * sizeof(PointLight));
	this->device.unmapMemory(this->lightsBufferMemory);

	std::vector<vk::DescriptorBufferInfo> descriptorBufferInfos = { vk::DescriptorBufferInfo{this->lightsBuffer, 0, bufferSize } };
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = { vk::WriteDescriptorSet{ this->globalDescriptorSet, 0, 0, vk::DescriptorType::eStorageBuffer, {}, descriptorBufferInfos } };
	this->device.updateDescriptorSets(writeDescriptorSets, {});
}

void VulkanRenderer::setMeshes(const std::vector<Mesh> meshes) {
	this->vertexBufferMutex.lock();
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
		this->meshes.push_back(meshPtr);
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
	this->vertexBufferMutex.unlock();
}

void VulkanRenderer::start() {
	this->running = true;
	this->renderThread = std::thread([this] { this->renderLoop(); });
}

void VulkanRenderer::drawMesh(const vk::CommandBuffer& cb, const Mesh& mesh, const glm::mat4& proj, const glm::mat4& view, const glm::vec3& cameraPos) {
	glm::mat4 model = glm::translate(mesh.translation) * glm::rotate(mesh.rotation.x, glm::vec3{ 0.0f, 0.0f, 1.0f }) * glm::rotate(mesh.rotation.y, glm::vec3{ 1.0f, 0.0f, 0.0f }) * glm::rotate(mesh.rotation.z, glm::vec3{ 0.0f, 1.0f, 0.0f }) * glm::scale(mesh.scale);
	std::vector<glm::mat4> pushConstantsMat = { proj * view * model, model };

	std::vector descriptorSets = { this->globalDescriptorSet, mesh.descriptorSet };
	cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, this->pipelineLayout, 0, descriptorSets, {});

	std::vector<glm::vec4> pushConstantsVec = { glm::vec4(cameraPos, 1.0f) };

	cb.pushConstants<glm::mat4x4>(this->pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pushConstantsMat);
	cb.pushConstants<glm::vec4>(this->pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4) * 2, pushConstantsVec);

	if (mesh.isIndexed)
		cb.drawIndexed(mesh.indices.size(), 1, mesh.firstIndex, 0, 0);
	else
		cb.draw(mesh.vertices.size(), 1, mesh.firstVertex, 0);
}

void VulkanRenderer::renderLoop() {
	const std::vector<vk::ClearValue> clearValues = { vk::ClearColorValue(std::array<float, 4>({0.0f, 0.0f, 0.0f, 1.0f})), vk::ClearDepthStencilValue{1.0f} };

	size_t frameIndex = 0;
	auto frameTime = std::chrono::high_resolution_clock::now();
	double runningTime = 0;

	glm::mat4 proj = glm::scale(glm::vec3{ 1.0f, -1.0f, 1.0f }) * glm::perspective(glm::radians(35.0f), this->swapchainExtent.width / (float)this->swapchainExtent.height, 0.1f, 1000.0f);

	while (this->running) {
		auto newFrameTime = std::chrono::high_resolution_clock::now();
		double deltaTime = std::chrono::duration<double>(newFrameTime - frameTime).count();
		runningTime += deltaTime;
		frameTime = newFrameTime;

		auto&& [r, imageIndex] = this->device.acquireNextImageKHR(this->swapchain, UINT64_MAX, this->acquireImageSemaphores[frameIndex]);
		vk::PipelineStageFlags pipelineStageFlags = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

		vk::CommandBuffer& cb = this->commandBuffers[imageIndex];
		cb.reset();
		cb.begin(vk::CommandBufferBeginInfo{});
		cb.beginRenderPass(vk::RenderPassBeginInfo{ this->renderPass, this->swapchainFramebuffers[imageIndex], vk::Rect2D({ 0, 0 }, this->swapchainExtent), clearValues }, vk::SubpassContents::eInline);
		cb.bindVertexBuffers(0, this->vertexIndexBuffer, { 0 });
		cb.bindIndexBuffer(this->vertexIndexBuffer, this->indexBufferOffset, vk::IndexType::eUint32);
		
		glm::vec3 cameraPos;
		glm::mat4 view;
		std::tie(cameraPos, view) = this->_camera.positionAndMatrix();
		
		cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->opaquePipeline);
		std::sort(this->opaqueMeshes.begin(), this->opaqueMeshes.end(), [&cameraPos](const std::shared_ptr<Mesh>& a, const std::shared_ptr<Mesh>& b) { return glm::distance(a->barycenter, cameraPos) < glm::distance(b->barycenter, cameraPos); }); //sort front-to-back
		for (auto& mesh : this->opaqueMeshes) {
			this->drawMesh(cb, *mesh, proj, view, cameraPos);
		}

		cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->blendPipeline);
		std::sort(this->nonOpaqueMeshes.begin(), this->nonOpaqueMeshes.end(), [&cameraPos](const std::shared_ptr<Mesh>& a, const std::shared_ptr<Mesh>& b) { return glm::distance(a->barycenter, cameraPos) > glm::distance(b->barycenter, cameraPos); }); //sort back-to-front
		for (auto& mesh : this->nonOpaqueMeshes) {
			this->drawMesh(cb, *mesh, proj, view, cameraPos);
		}


		cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, this->envPipelineLayout, 0, this->globalDescriptorSet, {});
		cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->envPipeline);
		glm::mat4 viewNoTrans = view;
		viewNoTrans[3] = glm::vec4(glm::vec3(0.0f), 1.0f);
		std::vector<glm::mat4> pushConstantsMat = { glm::inverse(proj * viewNoTrans) };
		cb.pushConstants<glm::mat4x4>(this->envPipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, pushConstantsMat);
		cb.draw(6, 1, 0, 0);

		cb.endRenderPass();
		cb.end();

		this->vertexBufferMutex.lock_shared();
		this->graphicsQueue.submit(vk::SubmitInfo{ this->acquireImageSemaphores[frameIndex], pipelineStageFlags, this->commandBuffers[imageIndex], this->presentSemaphores[frameIndex] }, this->frameFences[frameIndex]);
		this->graphicsQueue.presentKHR(vk::PresentInfoKHR(this->presentSemaphores[frameIndex], this->swapchain, imageIndex));

		this->device.waitForFences(this->frameFences[frameIndex], true, UINT64_MAX);
		this->device.resetFences(this->frameFences[frameIndex]);
		this->vertexBufferMutex.unlock_shared();

		frameIndex = (frameIndex + 1) % FRAMES_IN_FLIGHT;
	}
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
	this->device = this->physicalDevice.createDevice(vk::DeviceCreateInfo{ {}, queueCreateInfo, {}, deviceExtensions, &deviceFeatures });
	this->graphicsQueue = this->device.getQueue(this->graphicsQueueFamilyIndex, 0);

	this->commandPool = this->device.createCommandPool(vk::CommandPoolCreateInfo{{vk::CommandPoolCreateFlagBits::eResetCommandBuffer}, this->graphicsQueueFamilyIndex});
	this->allocator = vma::createAllocator(vma::AllocatorCreateInfo{ {}, this->physicalDevice, this->device });
}

void VulkanRenderer::createSwapchain() {
	auto surfaceCap = this->physicalDevice.getSurfaceCapabilitiesKHR(this->surface);
	auto surfaceFormats = this->physicalDevice.getSurfaceFormatsKHR(this->surface);

	vk::SurfaceFormatKHR selectedFormat = surfaceFormats[0];
	for (auto& format : surfaceFormats) {
		if (format.colorSpace == vk::ColorSpaceKHR::eHdr10St2084EXT) {
			selectedFormat = format;
			break;
		} else if (format.colorSpace == vk::ColorSpaceKHR::eBt2020LinearEXT) {
			selectedFormat = format;
		}
	}

	vk::SwapchainCreateInfoKHR createInfo{{}, this->surface, surfaceCap.minImageCount, selectedFormat.format, selectedFormat.colorSpace, surfaceCap.currentExtent, 1, vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive, {}};
	this->swapchain = this->device.createSwapchainKHR(createInfo);
	this->swapchainExtent = surfaceCap.currentExtent;
	this->swapchainFormat = selectedFormat.format;

	for (auto& image : this->device.getSwapchainImagesKHR(this->swapchain)) {
		vk::ComponentMapping ivMapping = { vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,vk::ComponentSwizzle::eIdentity,vk::ComponentSwizzle::eIdentity };
		vk::ImageSubresourceRange ivRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1);
		vk::ImageView iv = this->device.createImageView(vk::ImageViewCreateInfo{{}, image, vk::ImageViewType::e2D, selectedFormat.format, ivMapping, ivRange});
		this->swapchainImageViews.push_back(iv);
	}

	this->depthImage = this->device.createImage(vk::ImageCreateInfo{ {}, vk::ImageType::e2D, vk::Format::eD24UnormS8Uint, vk::Extent3D{this->swapchainExtent, 1}, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::SharingMode::eExclusive });

	auto memRequirements = this->device.getImageMemoryRequirements(this->depthImage);

	auto memProperties = this->physicalDevice.getMemoryProperties();

	uint32_t selectedMemoryTypeIndex = 0;
	for (auto&& [i, memoryType] : iter::enumerate(memProperties.memoryTypes)) {
		if (memoryType.propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) {
			selectedMemoryTypeIndex = i;
			break;
		}
	}

	this->depthImageMemory = this->device.allocateMemory(vk::MemoryAllocateInfo{memRequirements.size, selectedMemoryTypeIndex});
	this->device.bindImageMemory(this->depthImage, this->depthImageMemory, 0);
	
	vk::ImageSubresourceRange depthIvRange(vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1);
	this->depthImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->depthImage, vk::ImageViewType::e2D, vk::Format::eD24UnormS8Uint, vk::ComponentMapping{}, depthIvRange });

	this->commandBuffers = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, static_cast<uint32_t>(this->swapchainImageViews.size()) });
}

void VulkanRenderer::createRenderPass() {

	vk::AttachmentDescription colorAttachment{ {}, this->swapchainFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::ePresentSrcKHR };
	vk::AttachmentDescription depthAttachment{ {}, vk::Format::eD24UnormS8Uint, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eDontCare, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eDepthStencilAttachmentOptimal };
	std::vector<vk::AttachmentDescription> attachmentDescriptions = { colorAttachment, depthAttachment };

	vk::AttachmentReference colorAttachmentRef{ 0, vk::ImageLayout::eColorAttachmentOptimal };
	vk::AttachmentReference depthAttachmentRef{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal };
	vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRef, {}, &depthAttachmentRef);

	vk::SubpassDependency subpassDependency{ VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests, vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests, {}, vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite };

	this->renderPass = this->device.createRenderPass(vk::RenderPassCreateInfo{{}, attachmentDescriptions, subpass, subpassDependency});

	this->swapchainFramebuffers.reserve(this->swapchainImageViews.size());
	for (auto& iv : this->swapchainImageViews) {
		std::vector<vk::ImageView> attachments = { iv, this->depthImageView };
		vk::Framebuffer fb = this->device.createFramebuffer(vk::FramebufferCreateInfo{{}, this->renderPass, attachments, this->swapchainExtent.width, this->swapchainExtent.height, 1});
		this->swapchainFramebuffers.push_back(fb);
	}
}

void VulkanRenderer::createPipeline() {

	std::vector<uint32_t> vertexBytecode = readShaderFile("./shaders/basic.vert.spv");
	vk::ShaderModule vertexModule = this->device.createShaderModule(vk::ShaderModuleCreateInfo{ {}, vertexBytecode });
	vk::PipelineShaderStageCreateInfo vertexStageInfo = vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main"};

	std::vector<uint32_t> fragBytecode = readShaderFile("./shaders/pbr.frag.spv");
	vk::ShaderModule fragModule = this->device.createShaderModule(vk::ShaderModuleCreateInfo{ {}, fragBytecode });
	vk::PipelineShaderStageCreateInfo fragStageInfo = vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, fragModule, "main"};

	this->shaderModules = { vertexModule, fragModule };

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo, fragStageInfo };
	
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

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->swapchainExtent.width), static_cast<float>(this->swapchainExtent.height), 0.0f, 0.1f } };
	std::vector<vk::Rect2D> scissors = { vk::Rect2D({0, 0}, this->swapchainExtent) };

	vk::PipelineViewportStateCreateInfo viewportInfo{{}, viewports, scissors};

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{ {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f };

	vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{ {}, true, true, vk::CompareOp::eLess };

	vk::PipelineMultisampleStateCreateInfo multisampleInfo;

	vk::PipelineColorBlendAttachmentState opaqueColorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo opaqueColorBlendInfo{ {}, false, vk::LogicOp::eCopy, opaqueColorBlendAttachment };

	vk::PipelineColorBlendAttachmentState blendColorBlendAttachment(true, vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eSubtract, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo blendColorBlendInfo{ {}, false, vk::LogicOp::eCopy, blendColorBlendAttachment };
	
	//std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };
	std::vector<vk::DynamicState> dynamicStates = { };
	vk::PipelineDynamicStateCreateInfo dynamicStateInfo{{}, dynamicStates};


	std::vector<vk::PushConstantRange> pushConstantRanges = { vk::PushConstantRange{ vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(glm::mat4) * 2 + sizeof(glm::vec4) * 1 }, };

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
	};
	this->globalDescriptorSetLayout = this->device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, globalDescriptorSetBindings });

	const uint32_t maxObjectCount = 512u;
	std::vector< vk::DescriptorPoolSize> poolSizes = { vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 2 * maxObjectCount }, vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 3 + 5 * maxObjectCount }, vk::DescriptorPoolSize { vk::DescriptorType::eStorageBuffer, 1 } };
	this->descriptorPool = this->device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, maxObjectCount, poolSizes });
	std::vector<vk::DescriptorSetLayout> setLayouts = { this->globalDescriptorSetLayout, this->pbrDescriptorSetLayout };
	this->globalDescriptorSet = this->device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ descriptorPool, this->globalDescriptorSetLayout })[0];

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ {}, setLayouts, pushConstantRanges };

	this->pipelineLayout = this->device.createPipelineLayout(pipelineLayoutInfo);

	vk::PipelineCache pipelineCache = this->device.createPipelineCache(vk::PipelineCacheCreateInfo{});

	vk::Result r;
	std::tie(r, this->opaquePipeline) = this->device.createGraphicsPipeline(pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizationInfo, &multisampleInfo, &depthStencilInfo, &opaqueColorBlendInfo, &dynamicStateInfo, this->pipelineLayout, this->renderPass, 0 });
	std::tie(r, this->blendPipeline) = this->device.createGraphicsPipeline(pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizationInfo, &multisampleInfo, &depthStencilInfo, &blendColorBlendInfo, &dynamicStateInfo, this->pipelineLayout, this->renderPass, 0 });

	this->device.destroyPipelineCache(pipelineCache);
}

void VulkanRenderer::createVertexBuffer() {
	vk::CommandBuffer cb = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];
	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	size_t vertexBufferSize = 0u;
	size_t indexBufferSize = 0u;
	for (auto& mesh : this->meshes) {
		vertexBufferSize += mesh->vertices.size() * sizeof(Vertex);
		if (mesh->isIndexed)
			indexBufferSize += mesh->indices.size() * sizeof(uint32_t);
	}

	std::tie(this->vertexIndexBuffer, this->vertexIndexBufferAllocation) = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, vertexBufferSize + indexBufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly });

	auto [stagingBuffer, SBAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, vertexBufferSize + indexBufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuOnly });

	unsigned char* stagingData = reinterpret_cast<unsigned char*>(this->allocator.mapMemory(SBAllocation));
	size_t vbOffset = 0, ibOffset = 0;
	size_t vertexCount = 0, indexCount = 0;
	for (auto& mesh : this->meshes) {
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


void VulkanRenderer::createEnvPipeline() {
	std::vector<uint32_t> vertexBytecode = readShaderFile("./shaders/env.vert.spv");
	vk::ShaderModule vertexModule = this->device.createShaderModule(vk::ShaderModuleCreateInfo{ {}, vertexBytecode });
	vk::PipelineShaderStageCreateInfo vertexStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main" };
	this->shaderModules.push_back(vertexModule);

	std::vector<uint32_t> fragBytecode = readShaderFile("./shaders/env.frag.spv");
	vk::ShaderModule fragModule = this->device.createShaderModule(vk::ShaderModuleCreateInfo{ {}, fragBytecode });
	vk::PipelineShaderStageCreateInfo fragStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eFragment, fragModule, "main" };
	this->shaderModules.push_back(fragModule);

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo, fragStageInfo };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo{ {}, {}, {} };

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{ {}, vk::PrimitiveTopology::eTriangleList, false };

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->swapchainExtent.width), static_cast<float>(this->swapchainExtent.height), 0.0f, 0.1f } };
	std::vector<vk::Rect2D> scissors = { vk::Rect2D({0, 0}, this->swapchainExtent) };

	vk::PipelineViewportStateCreateInfo viewportInfo{ {}, viewports, scissors };

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{ {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f };

	vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{ {}, true, true, vk::CompareOp::eLess };

	vk::PipelineMultisampleStateCreateInfo multisampleInfo;

	vk::PipelineColorBlendAttachmentState colorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo{ {}, false, vk::LogicOp::eCopy, colorBlendAttachment };

	//std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };
	std::vector<vk::DynamicState> dynamicStates = { };
	vk::PipelineDynamicStateCreateInfo dynamicStateInfo{ {}, dynamicStates };


	std::vector<vk::PushConstantRange> pushConstantRanges = { vk::PushConstantRange{ vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(glm::mat4) * 1 }, };

	std::vector<vk::DescriptorSetLayout> setLayouts = { this->globalDescriptorSetLayout };

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ {}, setLayouts, pushConstantRanges };

	this->envPipelineLayout = this->device.createPipelineLayout(pipelineLayoutInfo);

	auto [r, pipeline] = this->device.createGraphicsPipeline(vk::PipelineCache(), vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizationInfo, &multisampleInfo, &depthStencilInfo, &colorBlendInfo, &dynamicStateInfo, this->envPipelineLayout, this->renderPass, 0 });
	this->envPipeline = pipeline;
}