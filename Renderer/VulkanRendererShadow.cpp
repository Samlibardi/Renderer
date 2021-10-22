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

void VulkanRenderer::createShadowMapRenderPass() {
	vk::AttachmentDescription depthAttachment{ {}, this->depthAttachmentFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal };
	std::vector<vk::AttachmentDescription> attachmentDescriptions = { depthAttachment };

	vk::AttachmentReference depthAttachmentRef{ 0, vk::ImageLayout::eDepthStencilAttachmentOptimal };

	vk::SubpassDescription subpass{ {}, vk::PipelineBindPoint::eGraphics };
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	std::vector<vk::SubpassDependency> subpassDependencies{
		//vk::SubpassDependency{VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eEarlyFragmentTests, vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::DependencyFlagBits::eByRegion},
		vk::SubpassDependency{0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eLateFragmentTests, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::AccessFlagBits::eShaderRead, vk::DependencyFlagBits::eByRegion},
	};

	this->shadowMapRenderPass = this->device.createRenderPass(vk::RenderPassCreateInfo{ {}, attachmentDescriptions, subpass, subpassDependencies });

	auto buffers = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, FRAMES_IN_FLIGHT });
	std::copy(buffers.begin(), buffers.end(), this->shadowPassCommandBuffers.begin());
}

void VulkanRenderer::createStaticShadowMapRenderPass() {
	vk::AttachmentDescription depthAttachment{ {}, this->depthAttachmentFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferSrcOptimal };
	std::vector<vk::AttachmentDescription> attachmentDescriptions = { depthAttachment };

	vk::AttachmentReference depthAttachmentRef{ 0, vk::ImageLayout::eDepthStencilAttachmentOptimal };

	vk::SubpassDescription subpass{ {}, vk::PipelineBindPoint::eGraphics };
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	std::vector<vk::SubpassDependency> subpassDependencies{
		vk::SubpassDependency{VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eEarlyFragmentTests, vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::DependencyFlagBits::eByRegion},
		vk::SubpassDependency{0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eLateFragmentTests, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::AccessFlagBits::eShaderRead, vk::DependencyFlagBits::eByRegion},
	};

	this->staticShadowMapRenderPass = this->device.createRenderPass(vk::RenderPassCreateInfo{ {}, attachmentDescriptions, subpass, subpassDependencies });
}

void VulkanRenderer::createDirectionalShadowMapRenderPass() {
	vk::AttachmentDescription depthAttachment{ {}, this->depthAttachmentFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eUndefined, vk::ImageLayout::eShaderReadOnlyOptimal };
	std::vector<vk::AttachmentDescription> attachmentDescriptions = { depthAttachment };

	vk::AttachmentReference depthAttachmentRef{ 0, vk::ImageLayout::eDepthStencilAttachmentOptimal };

	vk::SubpassDescription subpass{ {}, vk::PipelineBindPoint::eGraphics };
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	std::vector<vk::SubpassDependency> subpassDependencies{
		vk::SubpassDependency{0, VK_SUBPASS_EXTERNAL, vk::PipelineStageFlagBits::eLateFragmentTests, vk::PipelineStageFlagBits::eFragmentShader, vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::AccessFlagBits::eShaderRead, vk::DependencyFlagBits::eByRegion},
	};

	this->directionalShadowMapRenderPass = this->device.createRenderPass(vk::RenderPassCreateInfo{ {}, attachmentDescriptions, subpass, subpassDependencies });
}

void VulkanRenderer::createShadowMapPipeline() {
	this->shadowMapSampler = this->device.createSampler(vk::SamplerCreateInfo{ {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, vk::SamplerAddressMode::eClampToBorder, 0.0f, false, 0, true, vk::CompareOp::eLessOrEqual, 0.0f, VK_LOD_CLAMP_NONE, vk::BorderColor::eFloatOpaqueWhite });

	vk::ShaderModule vertexModule = this->loadShader("./shaders/shadowmap.vert.spv");
	vk::PipelineShaderStageCreateInfo vertexStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eVertex, vertexModule, "main" };
	this->shaderModules.push_back(vertexModule);

	std::vector<vk::PipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo };

	vk::VertexInputBindingDescription vertexBindingDescription(0, sizeof(Vertex), vk::VertexInputRate::eVertex);
	std::vector<vk::VertexInputAttributeDescription> vertexAttributeDescriptions = {
		vk::VertexInputAttributeDescription{ 0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos) },
	};

	vk::PipelineVertexInputStateCreateInfo vertexInputInfo{ {}, vertexBindingDescription, vertexAttributeDescriptions };

	vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo{ {}, vk::PrimitiveTopology::eTriangleList, false };

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->_settings.pointShadowMapResolution), static_cast<float>(this->_settings.pointShadowMapResolution), 0.0f, 1.0f } };
	std::vector<vk::Rect2D> scissors = { vk::Rect2D({0, 0}, vk::Extent2D{ this->_settings.pointShadowMapResolution, this->_settings.pointShadowMapResolution}) };

	vk::PipelineViewportStateCreateInfo viewportInfo{ {}, viewports, scissors };


	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{ {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eFront, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f };

	vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{ {}, true, true, vk::CompareOp::eLess };

	vk::PipelineMultisampleStateCreateInfo multisampleInfo;

	vk::PipelineColorBlendAttachmentState colorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo{ {}, false, vk::LogicOp::eCopy, colorBlendAttachment };


	std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
	vk::PipelineDynamicStateCreateInfo dynamicStateInfo{ {}, dynamicStates };


	std::vector<vk::PushConstantRange> pushConstantRanges = { vk::PushConstantRange{ vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4) * 2 }, };

	std::vector<vk::DescriptorSetLayout> setLayouts = { };

	this->shadowMapPipelineLayout = this->device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, setLayouts, pushConstantRanges });

	auto [r, pipeline] = this->device.createGraphicsPipeline(this->pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, & vertexInputInfo, & inputAssemblyInfo, nullptr, & viewportInfo, & rasterizationInfo, & multisampleInfo, & depthStencilInfo, & colorBlendInfo, & dynamicStateInfo, this->shadowMapPipelineLayout, this->shadowMapRenderPass, 0 });
	this->shadowMapPipeline = pipeline;
}

void VulkanRenderer::createShadowMapImage() {
	if (!this->shadowCastingPointLights.empty()) {
		std::tie(this->pointShadowMapsImage, this->pointShadowMapsImageAllocation) = this->allocator.createImage(
			vk::ImageCreateInfo{ {vk::ImageCreateFlagBits::eCubeCompatible}, vk::ImageType::e2D, this->depthAttachmentFormat, vk::Extent3D{this->_settings.pointShadowMapResolution, this->_settings.pointShadowMapResolution, 1}, 1, static_cast<uint32_t>(this->pointLights.size() * 6), vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive },
			vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
		);

		this->pointShadowMapFaceImageViews.reserve(this->shadowCastingPointLights.size() * 6);
		this->pointShadowMapFramebuffers.reserve(this->shadowCastingPointLights.size() * 6);
		for (const auto& [i, light] : iter::enumerate(this->shadowCastingPointLights)) {
			for (unsigned short j = 0; j < 6; j++) {
				vk::ImageView faceView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->pointShadowMapsImage, vk::ImageViewType::e2D, this->depthAttachmentFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, static_cast<uint32_t>(i * 6 + j), 1 } });
				this->pointShadowMapFaceImageViews.push_back(faceView);
				std::vector<vk::ImageView> attachments = { faceView };
				vk::Framebuffer fb = this->device.createFramebuffer(vk::FramebufferCreateInfo{ {}, this->shadowMapRenderPass, attachments, this->_settings.pointShadowMapResolution, this->_settings.pointShadowMapResolution, 1 });
				this->pointShadowMapFramebuffers.push_back(fb);
			}
		}

		this->pointShadowMapCubeArrayImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->pointShadowMapsImage, vk::ImageViewType::eCubeArray, this->depthAttachmentFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, 0, static_cast<uint32_t>(this->shadowCastingPointLights.size() * 6)} });
	}

	// parallel cascade split: https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus
	this->directionalShadowCascadeDepths.push_back(this->_camera.near());
	this->directionalShadowCascadeCameraSpaceDepths.push_back(0.0f);

	{
		const float f = this->_camera.far();
		const float n = this->_camera.near();

		for (int i = 1; i < this->directionalShadowCascadeLevels; i++) {
			const float& n = this->_camera.near();
			const float& f = this->_camera.far();
			const float N = static_cast<float>(this->directionalShadowCascadeLevels);

			const float Cuni = n + (f - n) * i / N;
			const float Clog = n * std::pow(f / n, i / N);

			const float lambda = 0.9f;

			float d = lambda * Clog + (1 - lambda) * Cuni;

			this->directionalShadowCascadeDepths.push_back(d);
			this->directionalShadowCascadeCameraSpaceDepths.push_back((f - f * n / d) / (f - n));
		}
	}

	std::tie(this->directionalCascadedShadowMapsImage, this->directionalCascadedShadowMapsImageAllocation) = this->allocator.createImage(
		vk::ImageCreateInfo{ {}, vk::ImageType::e2D, this->depthAttachmentFormat, vk::Extent3D{this->_settings.directionalShadowMapResolution, this->_settings.directionalShadowMapResolution, 1}, 1, directionalShadowCascadeLevels, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	for (uint32_t i = 0; i < this->directionalShadowCascadeLevels; i++) {
		vk::ImageView iv = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->directionalCascadedShadowMapsImage, vk::ImageViewType::e2D, this->depthAttachmentFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, i, 1 } });
		this->directionalShadowMapImageViews.push_back(iv);
		std::vector<vk::ImageView> attachments = { iv };
		vk::Framebuffer fb = this->device.createFramebuffer(vk::FramebufferCreateInfo{ {}, this->shadowMapRenderPass, attachments, this->_settings.directionalShadowMapResolution, this->_settings.directionalShadowMapResolution, 1 });
		this->directionalShadowMapFramebuffers.push_back(fb);
	}

	this->directionalShadowMapArrayImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->directionalCascadedShadowMapsImage, vk::ImageViewType::e2DArray, this->depthAttachmentFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, 0, VK_REMAINING_ARRAY_LAYERS} });

	size_t csmBufferSize = this->directionalShadowCascadeLevels * sizeof(CSMSplitShaderData);
	for (uint8_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		std::tie(this->directionalShadowCascadeSplitDataBuffers[i], this->directionalShadowCascadeSplitDataBufferAllocations[i]) = this->allocator.createBuffer(vk::BufferCreateInfo{ {}, csmBufferSize, vk::BufferUsageFlagBits::eStorageBuffer, vk::SharingMode::eExclusive, {} }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuToGpu });
	}

	std::vector<vk::DescriptorImageInfo> pointImageInfos = { vk::DescriptorImageInfo{this->shadowMapSampler, this->pointShadowMapCubeArrayImageView, vk::ImageLayout::eShaderReadOnlyOptimal } };
	std::vector<vk::DescriptorImageInfo> directionalImageInfos = { vk::DescriptorImageInfo{this->shadowMapSampler, this->directionalShadowMapArrayImageView, vk::ImageLayout::eShaderReadOnlyOptimal } };

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet{ this->globalDescriptorSet, 4, 0, vk::DescriptorType::eCombinedImageSampler, pointImageInfos },
		vk::WriteDescriptorSet{ this->globalDescriptorSet, 5, 0, vk::DescriptorType::eCombinedImageSampler, directionalImageInfos },
	};

	std::array<std::vector<vk::DescriptorBufferInfo>, FRAMES_IN_FLIGHT> csmBufferInfos;

	for (size_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		csmBufferInfos[i] = { vk::DescriptorBufferInfo{ this->directionalShadowCascadeSplitDataBuffers[i], 0, csmBufferSize } };
		writeDescriptorSets.push_back(vk::WriteDescriptorSet{ this->perFrameInFlightDescriptorSets[i], 1, 0, vk::DescriptorType::eStorageBuffer, {}, csmBufferInfos[i] });
	}

	this->device.updateDescriptorSets(writeDescriptorSets, {});
}

void VulkanRenderer::createStaticShadowMapImage() {
	if (!this->shadowCastingPointLights.empty()) {
		std::tie(this->staticPointShadowMapsImage, this->pointStaticShadowMapsImageAllocation) = this->allocator.createImage(
			vk::ImageCreateInfo{ {}, vk::ImageType::e2D, this->depthAttachmentFormat, vk::Extent3D{this->_settings.pointShadowMapResolution, this->_settings.pointShadowMapResolution, 1}, 1, static_cast<uint32_t>(this->pointLights.size() * 6), vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive },
			vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
		);

		this->pointShadowMapFaceImageViews.reserve(this->pointLights.size() * 6);
		this->pointShadowMapFramebuffers.reserve(this->pointLights.size() * 6);
		for (const auto& [i, light] : iter::enumerate(this->pointLights)) {
			for (unsigned short j = 0; j < 6; j++) {
				vk::ImageView faceView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->staticPointShadowMapsImage, vk::ImageViewType::e2D, this->depthAttachmentFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, static_cast<uint32_t>(i * 6 + j), 1 } });
				this->staticPointShadowMapFaceImageViews.push_back(faceView);
				std::vector<vk::ImageView> attachments = { faceView };
				vk::Framebuffer fb = this->device.createFramebuffer(vk::FramebufferCreateInfo{ {}, this->staticShadowMapRenderPass, attachments, this->_settings.pointShadowMapResolution, this->_settings.pointShadowMapResolution, 1 });
				this->staticPointShadowMapFramebuffers.push_back(fb);
			}
		}
	}
}

void VulkanRenderer::renderShadowMaps(uint32_t frameIndex) {
	vk::CommandBuffer& cb = this->shadowPassCommandBuffers[frameIndex];
	cb.reset();
	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	cb.bindVertexBuffers(0, this->vertexIndexBuffer, { 0 });
	cb.bindIndexBuffer(this->vertexIndexBuffer, this->indexBufferOffset, vk::IndexType::eUint32);

	cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->shadowMapPipeline);

	this->recordPointShadowMapsCommands(cb, frameIndex);
	this->recordDirectionalShadowMapsCommands(cb, frameIndex);

	cb.end();
	this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, this->shadowPassFinishedSemaphores[frameIndex] });
}

void VulkanRenderer::recordPointShadowMapsCommands(vk::CommandBuffer cb, uint32_t frameIndex) {
	if (this->shadowCastingPointLights.empty())
		return;

	static const std::vector<vk::ClearValue> clearValues = { vk::ClearDepthStencilValue{1.0f} };
	static const std::array<glm::quat, 6> faceRotations = {
		glm::quatLookAt(glm::vec3{+1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}),
		glm::quatLookAt(glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}),
		glm::quatLookAt(glm::vec3{0.0f, +1.0f, 0.0f}, glm::vec3{0.0f, 0.0f, -1.0f}),
		glm::quatLookAt(glm::vec3{0.0f, -1.0f, 0.0f}, glm::vec3{0.0f, 0.0f, +1.0f}),
		glm::quatLookAt(glm::vec3{0.0f, 0.0f, +1.0f}, glm::vec3{0.0f, 1.0f, 0.0f}),
		glm::quatLookAt(glm::vec3{0.0f, 0.0f, -1.0f}, glm::vec3{0.0f, 1.0f, 0.0f}),
	};
	std::array<Camera, 6> facePovs{};

	for (size_t i = 0; i < 6; i++) {
		facePovs[i] = Camera::Perspective({}, glm::eulerAngles(faceRotations[i]), 0.1f, 100.0f, glm::radians(90.0f), 1.0f);
		facePovs[i].setCropMatrix(glm::scale(glm::vec3{ -1.0f, 1.0f, 1.0f }));
	}

	cb.setViewport(0, vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->_settings.pointShadowMapResolution), static_cast<float>(this->_settings.pointShadowMapResolution), 0.0f, 1.0f });
	cb.setScissor(0, vk::Rect2D({ 0, 0 }, vk::Extent2D{ this->_settings.pointShadowMapResolution, this->_settings.pointShadowMapResolution }));

	for (auto&& [i, light] : iter::enumerate(this->shadowCastingPointLights)) {
		if (light->staticShadowMapRendered)
			continue;

		const glm::vec3& cameraPos = light->point;

		std::vector<std::shared_ptr<Mesh>> sortedMeshes;
		for (const auto& el : iter::sorted(this->staticMeshes, [&cameraPos](const std::shared_ptr<Mesh>& a, const std::shared_ptr<Mesh>& b) { return glm::distance(a->barycenter(), cameraPos) < glm::distance(b->barycenter(), cameraPos); }))
			sortedMeshes.push_back(el);

		for (unsigned short j = 0; j < 6; j++) {
			cb.beginRenderPass(vk::RenderPassBeginInfo{ this->staticShadowMapRenderPass, this->staticPointShadowMapFramebuffers[i * 6 + j], vk::Rect2D{{0, 0}, {this->_settings.pointShadowMapResolution, this->_settings.pointShadowMapResolution}}, clearValues }, vk::SubpassContents::eInline);

			auto& pov = facePovs[j];
			pov.setPosition(cameraPos);
			glm::mat4 viewproj = pov.viewProjMatrix();

			auto culledMeshes = iter::filter([this, &pov](const std::shared_ptr<Mesh> mesh) {
				return !this->cullMesh(*mesh, pov);
				}, sortedMeshes);


			for (auto& mesh : culledMeshes) {
				const glm::mat4 model = mesh->node->modelMatrix();
				const glm::mat4 modelviewproj = viewproj * model;

				std::vector<glm::mat4> pushConstants = { modelviewproj, model };

				cb.pushConstants<glm::mat4x4>(this->shadowMapPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstants);

				if (mesh->isIndexed)
					cb.drawIndexed(mesh->indices.size(), 1, mesh->firstIndexOffset, 0, 0);
				else
					cb.draw(mesh->vertices.size(), 1, mesh->firstVertexOffset, 0);
			}

			cb.endRenderPass();
		}

		light->staticShadowMapRendered = true;

		cb.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->pointShadowMapsImage, vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eDepth, 0, VK_REMAINING_MIP_LEVELS, static_cast<uint32_t>(i) * 6, 6 } });
		cb.blitImage(this->staticPointShadowMapsImage, vk::ImageLayout::eTransferSrcOptimal, this->pointShadowMapsImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageBlit{vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eDepth, 0, static_cast<uint32_t>(i) * 6, 6 }, std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->_settings.pointShadowMapResolution), static_cast<int32_t>(this->_settings.pointShadowMapResolution), 1 } }, vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eDepth, 0, static_cast<uint32_t>(i) * 6, 6 }, std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->_settings.pointShadowMapResolution), static_cast<int32_t>(this->_settings.pointShadowMapResolution), 1 } }}, vk::Filter::eNearest);
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->pointShadowMapsImage, vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eDepth, 0, VK_REMAINING_MIP_LEVELS, static_cast<uint32_t>(i) * 6, 6 } });
	}

	for (const auto& [i, light] : iter::enumerate(this->shadowCastingPointLights)) {
		const glm::vec3 cameraPos = light->point;

		std::vector<std::shared_ptr<Mesh>> sortedMeshes;
		for (const auto& el : iter::sorted(this->dynamicMeshes, [&cameraPos](const std::shared_ptr<Mesh>& a, const std::shared_ptr<Mesh>& b) { return glm::distance(a->barycenter(), cameraPos) < glm::distance(b->barycenter(), cameraPos); }))
			sortedMeshes.push_back(el);

		for (unsigned short j = 0; j < 6; j++) {

			auto& pov = facePovs[j];
			pov.setPosition(cameraPos);
			glm::mat4 viewproj = pov.viewProjMatrix();

			auto culledMeshes = iter::filter([this, &pov](const std::shared_ptr<Mesh> mesh) {
				return !this->cullMesh(*mesh, pov);
				}, sortedMeshes);

			uint32_t nonCulledMeshes = 0;
			for (auto& mesh : culledMeshes)
				nonCulledMeshes++;

			if (this->_settings.dynamicShadowsEnabled && nonCulledMeshes > 0) {
				cb.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->pointShadowMapsImage, vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eDepth, 0, VK_REMAINING_MIP_LEVELS, static_cast<uint32_t>(i) * 6 + j, 1 } });
				cb.blitImage(this->staticPointShadowMapsImage, vk::ImageLayout::eTransferSrcOptimal, this->pointShadowMapsImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageBlit{vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eDepth, 0, static_cast<uint32_t>(i) * 6 + j, 1 }, std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->_settings.pointShadowMapResolution), static_cast<int32_t>(this->_settings.pointShadowMapResolution), 1 } }, vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eDepth, 0, static_cast<uint32_t>(i) * 6 + j, 1 }, std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->_settings.pointShadowMapResolution), static_cast<int32_t>(this->_settings.pointShadowMapResolution), 1 } }}, vk::Filter::eNearest);
				cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eEarlyFragmentTests, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eDepthStencilAttachmentOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->pointShadowMapsImage, vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eDepth, 0, VK_REMAINING_MIP_LEVELS, static_cast<uint32_t>(i) * 6 + j, 1 } });

				cb.beginRenderPass(vk::RenderPassBeginInfo{ this->shadowMapRenderPass, this->pointShadowMapFramebuffers[i * 6 + j], vk::Rect2D{{0, 0}, {this->_settings.pointShadowMapResolution, this->_settings.pointShadowMapResolution}}, clearValues }, vk::SubpassContents::eInline);
				for (auto& mesh : culledMeshes) {
					const glm::mat4 model = mesh->node->modelMatrix();
					const glm::mat4 modelviewproj = viewproj * model;

					std::vector<glm::mat4> pushConstants = { modelviewproj, model };

					cb.pushConstants<glm::mat4x4>(this->shadowMapPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstants);

					if (mesh->isIndexed)
						cb.drawIndexed(mesh->indices.size(), 1, mesh->firstIndexOffset, 0, 0);
					else
						cb.draw(mesh->vertices.size(), 1, mesh->firstVertexOffset, 0);
				}
				cb.endRenderPass();
			}

		}
	}
}

void VulkanRenderer::recordDirectionalShadowMapsCommands(vk::CommandBuffer cb, uint32_t frameIndex) {
	static const std::vector<vk::ClearValue> clearValues = { vk::ClearDepthStencilValue{1.0f} };

	cb.setViewport(0, vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->_settings.directionalShadowMapResolution), static_cast<float>(this->_settings.directionalShadowMapResolution), 0.0f, 1.0f });
	cb.setScissor(0, vk::Rect2D({ 0, 0 }, vk::Extent2D{ this->_settings.directionalShadowMapResolution, this->_settings.directionalShadowMapResolution }));

	auto& light = this->directionalLight;

	Camera lightPov = light.lightViewCamera();

	CSMSplitShaderData* csmSplitsData = reinterpret_cast<CSMSplitShaderData*>(this->allocator.mapMemory(this->directionalShadowCascadeSplitDataBufferAllocations[frameIndex]));

	for (size_t i = 0; i < this->directionalShadowCascadeLevels; i++) {
		Camera viewSplit = this->_camera;
		viewSplit.setNear(this->directionalShadowCascadeDepths[i]);
		if(i < this->directionalShadowCascadeLevels - 1)
			viewSplit.setFar(this->directionalShadowCascadeDepths[i+1]);

		std::array<glm::vec3, 8> splitFrustumVerticesArray = viewSplit.getFrustumVertices();
		std::vector<glm::vec3> splitFrustumVertices(splitFrustumVerticesArray.begin(), splitFrustumVerticesArray.end());

		std::array<glm::vec3, 2> splitFrustumAABB = lightPov.makeAABBFromVertices(splitFrustumVertices);

		float largestSize = std::max(splitFrustumAABB[1].x - splitFrustumAABB[0].x, splitFrustumAABB[1].y - splitFrustumAABB[0].y);
		glm::vec3 offset = -glm::vec3{ (splitFrustumAABB[1].x + splitFrustumAABB[0].x) / 2 , (splitFrustumAABB[1].y + splitFrustumAABB[0].y) / 2, 0.0f };

		Camera splitPov = lightPov;
		splitPov.setCropMatrix(glm::scale(glm::vec3{ glm::vec2{ 1.0f / largestSize }, 1.0f }) * glm::translate(offset));
		
		glm::mat4 viewproj = splitPov.viewProjMatrix();
		csmSplitsData[i] = CSMSplitShaderData{ viewproj, this->directionalShadowCascadeCameraSpaceDepths[i] };

		std::vector<std::shared_ptr<Mesh>> sortedMeshes;
		for (const auto& el : iter::sorted(this->meshes, [&splitPov](const std::shared_ptr<Mesh>& a, const std::shared_ptr<Mesh>& b) { return glm::distance(a->barycenter(), splitPov.position()) < glm::distance(b->barycenter(), splitPov.position()); }))
			sortedMeshes.push_back(el);


		auto culledMeshes = iter::filter([this, &splitPov](const std::shared_ptr<Mesh> mesh) {
			return !this->cullMesh(*mesh, splitPov);
			}, sortedMeshes);

		uint32_t nonCulledMeshes = 0;
		for (auto& mesh : culledMeshes)
			nonCulledMeshes++;

		/*if (nonCulledMeshes > 0)*/ {
			cb.beginRenderPass(vk::RenderPassBeginInfo{ this->directionalShadowMapRenderPass, this->directionalShadowMapFramebuffers[i], vk::Rect2D{ {0, 0}, {this->_settings.directionalShadowMapResolution, this->_settings.directionalShadowMapResolution}}, clearValues }, vk::SubpassContents::eInline);
			for (auto& mesh : culledMeshes) {
				const glm::mat4 model = mesh->node->modelMatrix();
				const glm::mat4 modelviewproj = viewproj * model;

				std::vector<glm::mat4> pushConstants = { modelviewproj, model };

				cb.pushConstants<glm::mat4x4>(this->shadowMapPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstants);

				if (mesh->isIndexed)
					cb.drawIndexed(mesh->indices.size(), 1, mesh->firstIndexOffset, 0, 0);
				else
					cb.draw(mesh->vertices.size(), 1, mesh->firstVertexOffset, 0);
			}
			cb.endRenderPass();
		}
	}

	this->allocator.unmapMemory(this->directionalShadowCascadeSplitDataBufferAllocations[frameIndex]);
}