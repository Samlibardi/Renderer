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


void VulkanRenderer::createEnvPipeline() {
	vk::ShaderModule vertexModule = loadShader("./shaders/env.vert.spv");
	vk::PipelineShaderStageCreateInfo vertexStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main" };
	this->shaderModules.push_back(vertexModule);

	vk::ShaderModule fragModule = loadShader("./shaders/env.frag.spv");
	vk::PipelineShaderStageCreateInfo fragStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eFragment, fragModule, "main" };
	this->shaderModules.push_back(fragModule);

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo, fragStageInfo };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo{ {}, {}, {} };

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{ {}, vk::PrimitiveTopology::eTriangleList, false };

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->swapchainExtent.width), static_cast<float>(this->swapchainExtent.height), 0.0f, 1.0f } };
	std::vector<vk::Rect2D> scissors = { vk::Rect2D({0, 0}, this->swapchainExtent) };

	vk::PipelineViewportStateCreateInfo viewportInfo{ {}, viewports, scissors };

	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{ {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f };

	vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{ {}, true, true, vk::CompareOp::eLessOrEqual };

	vk::PipelineMultisampleStateCreateInfo multisampleInfo;

	vk::PipelineColorBlendAttachmentState colorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo{ {}, false, vk::LogicOp::eCopy, colorBlendAttachment };

	//std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };
	std::vector<vk::DynamicState> dynamicStates = { };
	vk::PipelineDynamicStateCreateInfo dynamicStateInfo{ {}, dynamicStates };


	std::vector<vk::PushConstantRange> pushConstantRanges = { };

	std::vector<vk::DescriptorSetLayout> setLayouts = { this->globalDescriptorSetLayout, this->cameraDescriptorSetLayout };

	this->envPipelineLayout = this->device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, setLayouts, pushConstantRanges });

	auto [r, pipeline] = this->device.createGraphicsPipeline(this->pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, & vertexInputInfo, & inputAssemblyInfo, nullptr, & viewportInfo, & rasterizationInfo, & multisampleInfo, & depthStencilInfo, & colorBlendInfo, & dynamicStateInfo, this->envPipelineLayout, this->renderPass, 0 });
	this->envPipeline = pipeline;
}

void VulkanRenderer::setEnvironmentMap(const std::array<TextureInfo, 6>& textureInfos) {
	std::tie(this->envMapImage, this->envMapAllocation) = this->allocator.createImage(
		vk::ImageCreateInfo{ {vk::ImageCreateFlagBits::eCubeCompatible}, vk::ImageType::e2D, this->envMapFormat, vk::Extent3D{textureInfos[0].width, textureInfos[0].height, 1}, 1, 6, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	this->envMapImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->envMapImage, vk::ImageViewType::eCube, this->envMapFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 } });

	auto [stagingBuffer, sbAllocation] = this->allocator.createBuffer(vk::BufferCreateInfo{ {},  textureInfos[0].data.size() * sizeof(byte), vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuOnly });
	//use a staging image to convert from 32bit float to envmapformat
	auto [stagingImage, siAllocation] = this->allocator.createImage(vk::ImageCreateInfo{ {}, vk::ImageType::e2D, vk::Format::eR32G32B32A32Sfloat, vk::Extent3D{textureInfos[0].width, textureInfos[0].height, 1}, 1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuOnly });

	void* sbData = this->allocator.mapMemory(sbAllocation);

	vk::CommandBuffer cb = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, 1 })[0];
	vk::Fence fence = this->device.createFence(vk::FenceCreateInfo{});

	for (auto&& [i, textureInfo] : iter::enumerate(textureInfos)) {
		memcpy(sbData, textureInfo.data.data(), textureInfo.data.size() * sizeof(byte));

		cb.reset();
		cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{ {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, stagingImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
		std::vector<vk::BufferImageCopy> copyRegions = { vk::BufferImageCopy{0, 0, 0, vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0}, {textureInfo.width, textureInfo.height, 1}  } };
		cb.copyBufferToImage(stagingBuffer, stagingImage, vk::ImageLayout::eTransferDstOptimal, copyRegions);
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{ {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->envMapImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, static_cast<uint32_t>(i), 1} });
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{ {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, stagingImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
		cb.blitImage(stagingImage, vk::ImageLayout::eTransferSrcOptimal, this->envMapImage, vk::ImageLayout::eTransferDstOptimal, { vk::ImageBlit{ vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1}, std::array<vk::Offset3D, 2>{ vk::Offset3D{0, 0, 0}, vk::Offset3D{static_cast<int32_t>(textureInfo.width), static_cast<int32_t>(textureInfo.height), 1} }, vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, static_cast<uint32_t>(i), 1}, std::array<vk::Offset3D, 2>{ vk::Offset3D{0, 0, 0}, vk::Offset3D{static_cast<int32_t>(textureInfo.width), static_cast<int32_t>(textureInfo.height), 1} }} }, vk::Filter::eNearest);
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->envMapImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, static_cast<uint32_t>(i), 1} });

		cb.end();

		this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
		this->device.waitForFences(fence, true, UINT64_MAX);
		this->device.resetFences(fence);
	}


	this->allocator.unmapMemory(sbAllocation);
	this->allocator.destroyBuffer(stagingBuffer, sbAllocation);
	this->allocator.destroyImage(stagingImage, siAllocation);
	this->device.destroyFence(fence);
	this->device.freeCommandBuffers(this->commandPool, cb);

	vk::DescriptorImageInfo envMapImageInfo{ this->textureSampler, this->envMapImageView, vk::ImageLayout::eShaderReadOnlyOptimal };

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet{ this->globalDescriptorSet, 1, 0, vk::DescriptorType::eCombinedImageSampler, envMapImageInfo },
	};
	this->device.updateDescriptorSets(writeDescriptorSets, {});

	this->makeDiffuseEnvMap();
	this->makeSpecularEnvMap();
}



std::tuple<vk::Pipeline, vk::PipelineLayout> VulkanRenderer::createEnvMapDiffuseBakePipeline(vk::RenderPass renderPass) {
	vk::ShaderModule vertexModule = loadShader("./shaders/envbake.vert.spv");
	vk::PipelineShaderStageCreateInfo vertexStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main" };
	this->shaderModules.push_back(vertexModule);

	vk::ShaderModule fragModule = loadShader("./shaders/envbakediffuse.frag.spv");
	vk::PipelineShaderStageCreateInfo fragStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eFragment, fragModule, "main" };
	this->shaderModules.push_back(fragModule);

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo, fragStageInfo };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo{ {}, {}, {} };

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{ {}, vk::PrimitiveTopology::eTriangleList, false };

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->envMapDiffuseResolution), static_cast<float>(this->envMapDiffuseResolution), 0.0f, 1.0f } };
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

	vk::PipelineLayout pipelineLayout = this->device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, setLayouts, pushConstantRanges });

	auto [r, pipeline] = this->device.createGraphicsPipeline(this->pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, & vertexInputInfo, & inputAssemblyInfo, nullptr, & viewportInfo, & rasterizationInfo, & multisampleInfo, & depthStencilInfo, & colorBlendInfo, & dynamicStateInfo, pipelineLayout, renderPass, 0 });

	return { pipeline, pipelineLayout };
}

std::tuple<vk::RenderPass, std::array<vk::ImageView, 6>, std::array<vk::Framebuffer, 6>> VulkanRenderer::createEnvMapDiffuseBakeRenderPass() {
	vk::AttachmentDescription colorAttachment{ {}, this->envMapFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal };
	std::vector<vk::AttachmentDescription> attachmentDescriptions = { colorAttachment };

	vk::AttachmentReference colorAttachmentRef{ 0, vk::ImageLayout::eColorAttachmentOptimal };
	vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRef, {});

	vk::SubpassDependency subpassDependency{ VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite };

	vk::RenderPass renderPass = this->device.createRenderPass(vk::RenderPassCreateInfo{ {}, attachmentDescriptions, subpass, subpassDependency });

	std::array<vk::Framebuffer, 6> framebuffers;
	std::array<vk::ImageView, 6> imageViews;
	for (unsigned short i = 0; i < 6; i++) {
		vk::ImageView iv = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->envMapDiffuseImage, vk::ImageViewType::e2D, this->envMapFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, i, 1} });
		imageViews[i] = iv;
		std::vector<vk::ImageView> attachments = { iv };
		vk::Framebuffer fb = this->device.createFramebuffer(vk::FramebufferCreateInfo{ {}, renderPass, attachments, this->envMapDiffuseResolution, this->envMapDiffuseResolution, 1 });
		framebuffers[i] = fb;
	}

	return { renderPass, imageViews, framebuffers };
}

void VulkanRenderer::makeDiffuseEnvMap() {
	std::tie(this->envMapDiffuseImage, this->envMapDiffuseAllocation) = this->allocator.createImage(
		vk::ImageCreateInfo{ {vk::ImageCreateFlagBits::eCubeCompatible}, vk::ImageType::e2D, this->envMapFormat, vk::Extent3D{this->envMapDiffuseResolution, this->envMapDiffuseResolution, 1}, 1, 6, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	this->envMapDiffuseImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->envMapDiffuseImage, vk::ImageViewType::eCube, this->envMapFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6 } });

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
	vk::ShaderModule vertexModule = loadShader("./shaders/envbake.vert.spv");
	vk::PipelineShaderStageCreateInfo vertexStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main" };
	this->shaderModules.push_back(vertexModule);

	vk::ShaderModule fragModule = loadShader("./shaders/envbakespecular.frag.spv");
	vk::PipelineShaderStageCreateInfo fragStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eFragment, fragModule, "main" };
	this->shaderModules.push_back(fragModule);

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo, fragStageInfo };

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo{ {}, {}, {} };

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{ {}, vk::PrimitiveTopology::eTriangleList, false };

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->envMapDiffuseResolution), static_cast<float>(this->envMapDiffuseResolution), 0.0f, 1.0f  } };
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

	vk::PipelineLayout pipelineLayout = this->device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, setLayouts, pushConstantRanges });

	auto [r, pipeline] = this->device.createGraphicsPipeline(this->pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, & vertexInputInfo, & inputAssemblyInfo, nullptr, & viewportInfo, & rasterizationInfo, & multisampleInfo, & depthStencilInfo, & colorBlendInfo, & dynamicStateInfo, pipelineLayout, renderPass, 0 });

	return { pipeline, pipelineLayout };
}

std::tuple<vk::RenderPass, std::array<std::array<vk::ImageView, 10>, 6>, std::array<std::array<vk::Framebuffer, 10>, 6>> VulkanRenderer::createEnvMapSpecularBakeRenderPass() {
	vk::AttachmentDescription colorAttachment{ {}, this->envMapFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal };
	std::vector<vk::AttachmentDescription> attachmentDescriptions = { colorAttachment };

	vk::AttachmentReference colorAttachmentRef{ 0, vk::ImageLayout::eColorAttachmentOptimal };
	vk::SubpassDescription subpass({}, vk::PipelineBindPoint::eGraphics, {}, colorAttachmentRef, {});

	vk::SubpassDependency subpassDependency{ VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, vk::AccessFlagBits::eColorAttachmentWrite };

	vk::RenderPass renderPass = this->device.createRenderPass(vk::RenderPassCreateInfo{ {}, attachmentDescriptions, subpass, subpassDependency });

	std::array <std::array<vk::Framebuffer, 10>, 6> framebuffers;
	std::array<std::array<vk::ImageView, 10>, 6> imageViews;
	for (unsigned short face = 0; face < 6; face++) {
		for (unsigned short roughnessLevel = 0; roughnessLevel < 10; roughnessLevel++) {
			vk::ImageView iv = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->envMapSpecularImage, vk::ImageViewType::e2D, this->envMapFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, roughnessLevel, 1, face, 1} });
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
		vk::ImageCreateInfo{ {vk::ImageCreateFlagBits::eCubeCompatible}, vk::ImageType::e2D, this->envMapFormat, vk::Extent3D{this->envMapSpecularResolution, this->envMapSpecularResolution, 1}, 10, 6, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	this->envMapSpecularImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->envMapSpecularImage, vk::ImageViewType::eCube, this->envMapFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 10, 0, 6 } });

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
			cb.setViewport(0, vk::Viewport{ 0.0f, 0.0f, static_cast<float>(renderRes), static_cast<float>(renderRes), 0.0f, 1.0f });
			cb.setScissor(0, vk::Rect2D{ {0, 0}, {renderRes, renderRes} });
			glm::mat4 view = views[face];
			cb.pushConstants<glm::mat4>(pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, glm::inverse(proj * view));
			cb.pushConstants<float>(pipelineLayout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, sizeof(glm::mat4), static_cast<float>(roughnessLevel) / 9);
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


	vk::DescriptorImageInfo envMapSpecularImageInfo{ this->textureSampler, this->envMapSpecularImageView, vk::ImageLayout::eShaderReadOnlyOptimal };
	vk::DescriptorImageInfo brdfLUTImageInfo{ this->textureSampler, this->BRDFLutTexture.imageView(), vk::ImageLayout::eShaderReadOnlyOptimal };

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet{ this->globalDescriptorSet, 1, 0, vk::DescriptorType::eCombinedImageSampler, envMapSpecularImageInfo },
		vk::WriteDescriptorSet{ this->globalDescriptorSet, 3, 0, vk::DescriptorType::eCombinedImageSampler, brdfLUTImageInfo },
	};
	this->device.updateDescriptorSets(writeDescriptorSets, {});
}
