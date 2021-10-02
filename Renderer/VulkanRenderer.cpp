#include <fstream>
#include <chrono>

#include <itertools/enumerate.hpp>

#include "VulkanRenderer.h"

#include <glm/mat4x4.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtx/normal.hpp>

#include "Vertex.h"
#include "Mesh.h"
#include "PointLight.h"

const int FRAMES_IN_FLIGHT = 2;

VulkanRenderer::VulkanRenderer() {
	vkfw::init();

	this->window = vkfw::createWindow(1280, 800, "Hello Vulkan", {}, {});
	this->window.set<vkfw::Attribute::eResizable>(false);

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
	this->createSwapchain();
	this->createRenderPass();
	this->createPipeline();
	this->textureSampler = this->device.createSampler(vk::SamplerCreateInfo{ {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, 0.0f, true, this->physicalDevice.getProperties().limits.maxSamplerAnisotropy, false });

	this->acquireImageSemaphores.reserve(FRAMES_IN_FLIGHT);
	this->presentSemaphores.reserve(FRAMES_IN_FLIGHT);
	this->frameFences.reserve(FRAMES_IN_FLIGHT);
	for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		this->acquireImageSemaphores.push_back(this->device.createSemaphore(vk::SemaphoreCreateInfo{}));
		this->presentSemaphores.push_back(this->device.createSemaphore(vk::SemaphoreCreateInfo{}));
		this->frameFences.push_back(this->device.createFence(vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled}));
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

	this->device.destroyPipeline(this->pipeline);
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
	if (textureInfo.width > 512 && textureInfo.height > 512) mipLevels = 4;

	auto [image, allocation] =  this->allocator.createImage(
		vk::ImageCreateInfo{ {}, vk::ImageType::e2D, vk::Format::eR8G8B8A8Unorm, vk::Extent3D{textureInfo.width, textureInfo.height, 1}, mipLevels, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	vk::ImageView imageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, image, vk::ImageViewType::e2D, vk::Format::eR8G8B8A8Unorm, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } });

	return { image, imageView, allocation };
}

void VulkanRenderer::stageTexture(vk::Image image, TextureInfo& textureInfo) {

	auto [stagingBuffer, sbAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {},  textureInfo.data.size() * sizeof(byte), vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuToGpu });

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
	this->device.destroyFence(fence);
}

void VulkanRenderer::setPBRParameters(PBRInfo parameters)
{
	PBRInfo& pbrBufferData = *reinterpret_cast<PBRInfo*>(this->allocator.mapMemory(this->pbrBufferAllocation));
	pbrBufferData = parameters;
	this->allocator.unmapMemory(this->pbrBufferAllocation);
}

void VulkanRenderer::setTextures(TextureInfo albedoInfo, TextureInfo normalInfo, TextureInfo metallicRoughnessInfo, TextureInfo aoInfo, TextureInfo emissiveInfo) {
	auto albedo = this->createImageFromTextureInfo(albedoInfo);
	auto normal = this->createImageFromTextureInfo(normalInfo);
	auto metallicRoughness = this->createImageFromTextureInfo(metallicRoughnessInfo);
	auto ao = this->createImageFromTextureInfo(aoInfo);
	auto emissive = this->createImageFromTextureInfo(emissiveInfo);

	this->stageTexture(std::get<0>(albedo), albedoInfo);
	this->stageTexture(std::get<0>(normal), normalInfo);
	this->stageTexture(std::get<0>(metallicRoughness), metallicRoughnessInfo);
	this->stageTexture(std::get<0>(ao), aoInfo);
	this->stageTexture(std::get<0>(emissive), emissiveInfo);

	std::vector<vk::DescriptorImageInfo> albedoImageInfos = { vk::DescriptorImageInfo{this->textureSampler, std::get<1>(albedo), vk::ImageLayout::eShaderReadOnlyOptimal } };
	std::vector<vk::DescriptorImageInfo> normalImageInfos = { vk::DescriptorImageInfo{this->textureSampler, std::get<1>(normal), vk::ImageLayout::eShaderReadOnlyOptimal } };
	std::vector<vk::DescriptorImageInfo> metallicRoughnessImageInfos = { vk::DescriptorImageInfo{this->textureSampler, std::get<1>(metallicRoughness), vk::ImageLayout::eShaderReadOnlyOptimal } };
	std::vector<vk::DescriptorImageInfo> aoImageInfos = { vk::DescriptorImageInfo{this->textureSampler, std::get<1>(ao), vk::ImageLayout::eShaderReadOnlyOptimal } };
	std::vector<vk::DescriptorImageInfo> emissiveImageInfos = { vk::DescriptorImageInfo{this->textureSampler, std::get<1>(emissive), vk::ImageLayout::eShaderReadOnlyOptimal } };

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = { 
		vk::WriteDescriptorSet{ this->descriptorSets[1], 1, 0, vk::DescriptorType::eCombinedImageSampler, albedoImageInfos },
		vk::WriteDescriptorSet{ this->descriptorSets[1], 2, 0, vk::DescriptorType::eCombinedImageSampler, normalImageInfos },
		vk::WriteDescriptorSet{ this->descriptorSets[1], 3, 0, vk::DescriptorType::eCombinedImageSampler, metallicRoughnessImageInfos },
		vk::WriteDescriptorSet{ this->descriptorSets[1], 4, 0, vk::DescriptorType::eCombinedImageSampler, aoImageInfos },
		vk::WriteDescriptorSet{ this->descriptorSets[1], 5, 0, vk::DescriptorType::eCombinedImageSampler, emissiveImageInfos },
	};
	this->device.updateDescriptorSets(writeDescriptorSets, {});

	this->textures.push_back(albedo);
	this->textures.push_back(normal);
	this->textures.push_back(metallicRoughness);
	this->textures.push_back(ao);
	this->textures.push_back(emissive);
}

void VulkanRenderer::setLights(std::vector<PointLight> lights) {

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
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = { vk::WriteDescriptorSet{ this->descriptorSets[0], 0, 0, vk::DescriptorType::eStorageBuffer, {}, descriptorBufferInfos } };
	this->device.updateDescriptorSets(writeDescriptorSets, {});
}

void VulkanRenderer::setMesh(Mesh & mesh) {
	this->destroyVertexBuffer();
	this->mesh = mesh;
	this->createVertexBuffer();
}

void VulkanRenderer::run() {
	this->running = true;
	this->renderThread = std::thread([this] { this->renderLoop(); });

	while (!this->window.shouldClose()) {
		vkfw::pollEvents();
		std::this_thread::sleep_for(std::chrono::milliseconds(6));
	}

	this->running = false;
	this->renderThread.join();
}

void VulkanRenderer::renderLoop() {
	const std::vector<vk::ClearValue> clearValues = { vk::ClearColorValue(std::array<float, 4>({0.0f, 0.0f, 0.0f, 1.0f})), vk::ClearDepthStencilValue{1.0f} };

	size_t frameIndex = 0;
	auto frameTime = std::chrono::high_resolution_clock::now();
	double runningTime = 0;
	while (this->running) {
		auto newFrameTime = std::chrono::high_resolution_clock::now();
		double deltaTime = std::chrono::duration<double>(newFrameTime - frameTime).count();
		runningTime += deltaTime;
		frameTime = newFrameTime;

		this->device.waitForFences(this->frameFences[frameIndex], true, UINT64_MAX);
		this->device.resetFences(this->frameFences[frameIndex]);
		auto&& [r, imageIndex] = this->device.acquireNextImageKHR(this->swapchain, UINT64_MAX, this->acquireImageSemaphores[frameIndex]);
		vk::PipelineStageFlags pipelineStageFlags = { vk::PipelineStageFlagBits::eColorAttachmentOutput };

		vk::CommandBuffer& cb = this->commandBuffers[imageIndex];
		cb.reset();
		cb.begin(vk::CommandBufferBeginInfo{});
		cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, this->pipelineLayout, 0, this->descriptorSets, {});
		cb.beginRenderPass(vk::RenderPassBeginInfo{ this->renderPass, this->swapchainFramebuffers[imageIndex], vk::Rect2D({ 0, 0 }, this->swapchainExtent), clearValues }, vk::SubpassContents::eInline);
		cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->pipeline);
		cb.bindVertexBuffers(0, this->vertexBuffer, { 0 });
		cb.bindIndexBuffer(this->indexBuffer, 0, vk::IndexType::eUint32);
		glm::mat4 proj = glm::scale(glm::vec3{ 1.0f, -1.0f, 1.0f }) *  glm::perspective(glm::radians(60.0f), this->swapchainExtent.width / (float)this->swapchainExtent.height, 0.1f, 1000.0f);
		glm::vec3 cameraPos{ 0.0f, 0.2f, 3.0f };
		glm::mat4 view = glm::translate(-cameraPos) * glm::lookAt(cameraPos, glm::vec3{ 0.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f });
		glm::mat4 model = glm::translate(glm::vec3{0.0f, -1.0f, 0.0f}) * glm::rotate<float>(1.5f * runningTime, glm::vec3(0.0, 1.0, 0.0)) *glm::scale(glm::vec3(1.5f));
		std::vector<glm::mat4> pushConstantsMat = { proj * view * model, model };
		float roughness = 0.5f + glm::cos(0.33f * runningTime) / 2.0f;
		std::vector<glm::vec4> pushConstantsVec = { glm::vec4(cameraPos, 1.0f) };
		cb.pushConstants<glm::mat4x4>(this->pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment , 0, pushConstantsMat);
		cb.pushConstants<glm::vec4>(this->pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4)*2, pushConstantsVec);

		if (mesh.isIndexed)
			cb.drawIndexed(this->mesh.triangleIndices.size(), 1, 0, 0, 0);
		else
			cb.draw(this->mesh.vertices.size(), 1, 0, 0);

		cb.endRenderPass();
		cb.end();

		this->graphicsQueue.submit(vk::SubmitInfo{ this->acquireImageSemaphores[frameIndex], pipelineStageFlags, this->commandBuffers[imageIndex], this->presentSemaphores[frameIndex] }, this->frameFences[frameIndex]);
		this->graphicsQueue.presentKHR(vk::PresentInfoKHR(this->presentSemaphores[frameIndex], this->swapchain, imageIndex));
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
		if (format.format == vk::Format::eA2B10G10R10SnormPack32 || format.format == vk::Format::eA2B10G10R10UnormPack32) {
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

std::vector<uint32_t> readShaderFile(std::string && path) {
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

	vk::PipelineColorBlendAttachmentState colorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo{{}, false, vk::LogicOp::eCopy, colorBlendAttachment};
	
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
	};
	this->pbrDescriptorSetLayout = this->device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, pbrSetBindings });

	std::vector<vk::DescriptorSetLayoutBinding> globalDescriptorSetBindings = {  vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment} };
	this->globalDescriptorSetLayout = this->device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, globalDescriptorSetBindings });

	std::vector< vk::DescriptorPoolSize> poolSizes = { vk::DescriptorPoolSize{ vk::DescriptorType::eUniformBuffer, 1 }, vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, 5 }, vk::DescriptorPoolSize { vk::DescriptorType::eStorageBuffer, 1 } };
	this->descriptorPool = this->device.createDescriptorPool(vk::DescriptorPoolCreateInfo{ {}, 2, poolSizes });
	std::vector<vk::DescriptorSetLayout> setLayouts = { this->globalDescriptorSetLayout, this->pbrDescriptorSetLayout };
	this->descriptorSets = this->device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ descriptorPool, setLayouts });

	
	std::tie(this->pbrBuffer, this->pbrBufferAllocation) = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, sizeof(PBRInfo), vk::BufferUsageFlagBits::eUniformBuffer, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuToGpu });

	PBRInfo& pbrBufferData = *reinterpret_cast<PBRInfo*>(this->allocator.mapMemory(this->pbrBufferAllocation));
	pbrBufferData = { glm::vec4(1.0f), glm::vec4(0.0f), 1.0f, 1.0f, 1.0f };
	this->allocator.unmapMemory(this->pbrBufferAllocation);

	std::vector<vk::DescriptorBufferInfo> descriptorBufferInfos = { vk::DescriptorBufferInfo{this->pbrBuffer, 0, sizeof(PBRInfo) } };
	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = { vk::WriteDescriptorSet{ this->descriptorSets[1], 0, 0, vk::DescriptorType::eUniformBuffer, {}, descriptorBufferInfos } };
	this->device.updateDescriptorSets(writeDescriptorSets, {});

	vk::PipelineLayoutCreateInfo pipelineLayoutInfo{ {}, setLayouts, pushConstantRanges };

	this->pipelineLayout = this->device.createPipelineLayout(pipelineLayoutInfo);

	auto [r, pipeline] = this->device.createGraphicsPipeline(vk::PipelineCache(), vk::GraphicsPipelineCreateInfo{{}, shaderStagesInfo, &vertexInputInfo, &inputAssemblyInfo, nullptr, &viewportInfo, &rasterizationInfo, &multisampleInfo, &depthStencilInfo, &colorBlendInfo, &dynamicStateInfo, this->pipelineLayout, this->renderPass, 0});
	this->pipeline = pipeline;
}

void VulkanRenderer::createVertexBuffer() {
	size_t vertexBufferSize = this->mesh.vertices.size() * sizeof(Vertex);
	std::tie(this->vertexBuffer, this->vertexBufferAllocation) = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, vertexBufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly });

	size_t indexBufferSize = this->mesh.triangleIndices.size() * sizeof(uint32_t);
	std::tie(this->indexBuffer, this->indexBufferAllocation) = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, indexBufferSize, vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly });

	auto [vertexStagingBuffer, vertexSBAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, vertexBufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuToGpu });
	auto [indexStagingBuffer, indexSBAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, indexBufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuToGpu });
	
	void* vertexData = this->allocator.mapMemory(vertexSBAllocation);
	void* indexData = this->allocator.mapMemory(indexSBAllocation);
	memcpy(vertexData, this->mesh.vertices.data(), vertexBufferSize);
	memcpy(indexData, this->mesh.triangleIndices.data(), indexBufferSize);
	this->allocator.unmapMemory(vertexSBAllocation);
	this->allocator.unmapMemory(indexSBAllocation);

	vk::CommandBuffer cb = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];

	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	cb.copyBuffer(vertexStagingBuffer, this->vertexBuffer, vk::BufferCopy{0, 0, vertexBufferSize});
	cb.copyBuffer(indexStagingBuffer, this->indexBuffer, vk::BufferCopy{0, 0, indexBufferSize});
	cb.end();

	vk::Fence fence = this->device.createFence(vk::FenceCreateInfo{});
	this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
	this->device.waitForFences(fence, true, UINT64_MAX);
	this->allocator.destroyBuffer(vertexStagingBuffer, vertexSBAllocation);
	this->allocator.destroyBuffer(indexStagingBuffer, indexSBAllocation);
	this->device.destroyFence(fence);
}

void VulkanRenderer::destroyVertexBuffer() {
	this->allocator.destroyBuffer(this->vertexBuffer, this->vertexBufferAllocation);
	this->allocator.destroyBuffer(this->indexBuffer, this->indexBufferAllocation);
}