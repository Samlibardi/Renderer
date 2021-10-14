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

bool cullMesh(const Mesh& mesh, const glm::mat4& modelviewproj);

void VulkanRenderer::createShadowMapRenderPass() {
	vk::AttachmentDescription depthAttachment{ {}, this->depthAttachmentFormat, vk::SampleCountFlagBits::e1, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore, vk::AttachmentLoadOp::eDontCare, vk::AttachmentStoreOp::eDontCare, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal };
	std::vector<vk::AttachmentDescription> attachmentDescriptions = { depthAttachment };

	vk::AttachmentReference depthAttachmentRef{ 0, vk::ImageLayout::eDepthStencilAttachmentOptimal };

	vk::SubpassDescription subpass{ {}, vk::PipelineBindPoint::eGraphics };
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	std::vector<vk::SubpassDependency> subpassDependencies{
		vk::SubpassDependency{VK_SUBPASS_EXTERNAL, 0, vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eEarlyFragmentTests, vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eDepthStencilAttachmentWrite, vk::DependencyFlagBits::eByRegion},
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

void VulkanRenderer::createShadowMapPipeline() {
	this->shadowMapSampler = this->device.createSampler(vk::SamplerCreateInfo{ {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eLinear, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, vk::SamplerAddressMode::eRepeat, 0.0f, true, this->physicalDevice.getProperties().limits.maxSamplerAnisotropy, true, vk::CompareOp::eLessOrEqual, 0.0f, VK_LOD_CLAMP_NONE });

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

	std::vector<vk::Viewport> viewports = { vk::Viewport{ 0.0f, 0.0f, static_cast<float>(this->_settings.shadowMapResolution), static_cast<float>(this->_settings.shadowMapResolution), 0.0f, 1.0f } };
	std::vector<vk::Rect2D> scissors = { vk::Rect2D({0, 0}, vk::Extent2D{ this->_settings.shadowMapResolution, this->_settings.shadowMapResolution}) };

	vk::PipelineViewportStateCreateInfo viewportInfo{ {}, viewports, scissors };


	vk::PipelineRasterizationStateCreateInfo rasterizationInfo{ {}, false, false, vk::PolygonMode::eFill, vk::CullModeFlagBits::eFront, vk::FrontFace::eCounterClockwise, false, 0.0f, 0.0f, 0.0f, 1.0f };

	vk::PipelineDepthStencilStateCreateInfo depthStencilInfo{ {}, true, true, vk::CompareOp::eLess };

	vk::PipelineMultisampleStateCreateInfo multisampleInfo;

	vk::PipelineColorBlendAttachmentState colorBlendAttachment(false, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eZero, vk::BlendOp::eAdd, vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);
	vk::PipelineColorBlendStateCreateInfo colorBlendInfo{ {}, false, vk::LogicOp::eCopy, colorBlendAttachment };

	//std::vector<vk::DynamicState> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eLineWidth };
	std::vector<vk::DynamicState> dynamicStates = { };
	vk::PipelineDynamicStateCreateInfo dynamicStateInfo{ {}, dynamicStates };


	std::vector<vk::PushConstantRange> pushConstantRanges = { vk::PushConstantRange{ vk::ShaderStageFlagBits::eVertex, 0, sizeof(glm::mat4) * 2 }, };

	std::vector<vk::DescriptorSetLayout> setLayouts = { };

	this->shadowMapPipelineLayout = this->device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, setLayouts, pushConstantRanges });

	auto [r, pipeline] = this->device.createGraphicsPipeline(this->pipelineCache, vk::GraphicsPipelineCreateInfo{ {}, shaderStagesInfo, & vertexInputInfo, & inputAssemblyInfo, nullptr, & viewportInfo, & rasterizationInfo, & multisampleInfo, & depthStencilInfo, & colorBlendInfo, & dynamicStateInfo, this->shadowMapPipelineLayout, this->shadowMapRenderPass, 0 });
	this->shadowMapPipeline = pipeline;
}

void VulkanRenderer::createShadowMapImage() {
	uint32_t mipLevels = 1u;
	uint32_t maxDim = this->_settings.shadowMapResolution;
	//if (maxDim > 1u) mipLevels = std::floor(std::log2(maxDim)) + 1;

	std::tie(this->shadowMapImage, this->shadowMapImageAllocation) = this->allocator.createImage(
		vk::ImageCreateInfo{ {vk::ImageCreateFlagBits::eCubeCompatible}, vk::ImageType::e2D, this->depthAttachmentFormat, vk::Extent3D{this->_settings.shadowMapResolution, this->_settings.shadowMapResolution, 1}, mipLevels, static_cast<uint32_t>(this->lights.size() * 6), vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	this->shadowMapFaceImageViews.reserve(this->lights.size() * 6);
	this->shadowMapFramebuffers.reserve(this->lights.size() * 6);
	for (const auto& [i, light] : iter::enumerate(this->lights)) {
		for (unsigned short j = 0; j < 6; j++) {
			vk::ImageView faceView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->shadowMapImage, vk::ImageViewType::e2D, this->depthAttachmentFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, static_cast<uint32_t>(i * 6 + j), 1 } });
			this->shadowMapFaceImageViews.push_back(faceView);
			std::vector<vk::ImageView> attachments = { faceView };
			vk::Framebuffer fb = this->device.createFramebuffer(vk::FramebufferCreateInfo{ {}, this->shadowMapRenderPass, attachments, this->_settings.shadowMapResolution, this->_settings.shadowMapResolution, 1 });
			this->shadowMapFramebuffers.push_back(fb);
		}
	}

	this->shadowMapCubeArrayImageView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->shadowMapImage, vk::ImageViewType::eCubeArray, this->depthAttachmentFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, mipLevels, 0, static_cast<uint32_t>(this->lights.size() * 6)} });

	std::vector<vk::DescriptorImageInfo> imageInfos = { vk::DescriptorImageInfo{this->shadowMapSampler, this->shadowMapCubeArrayImageView, vk::ImageLayout::eShaderReadOnlyOptimal } };

	std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
		vk::WriteDescriptorSet{ this->globalDescriptorSet, 4, 0, vk::DescriptorType::eCombinedImageSampler, imageInfos },
	};
	this->device.updateDescriptorSets(writeDescriptorSets, {});
}

void VulkanRenderer::createStaticShadowMapImage() {
	uint32_t mipLevels = 1u;
	uint32_t maxDim = this->_settings.shadowMapResolution;
	//if (maxDim > 1u) mipLevels = std::floor(std::log2(maxDim)) + 1;

	std::tie(this->staticShadowMapImage, this->staticShadowMapImageAllocation) = this->allocator.createImage(
		vk::ImageCreateInfo{ {}, vk::ImageType::e2D, this->depthAttachmentFormat, vk::Extent3D{this->_settings.shadowMapResolution, this->_settings.shadowMapResolution, 1}, mipLevels, static_cast<uint32_t>(this->lights.size() * 6), vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	this->shadowMapFaceImageViews.reserve(this->lights.size() * 6);
	this->shadowMapFramebuffers.reserve(this->lights.size() * 6);
	for (const auto& [i, light] : iter::enumerate(this->lights)) {
		for (unsigned short j = 0; j < 6; j++) {
			vk::ImageView faceView = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->staticShadowMapImage, vk::ImageViewType::e2D, this->depthAttachmentFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, static_cast<uint32_t>(i * 6 + j), 1 } });
			this->staticShadowMapFaceImageViews.push_back(faceView);
			std::vector<vk::ImageView> attachments = { faceView };
			vk::Framebuffer fb = this->device.createFramebuffer(vk::FramebufferCreateInfo{ {}, this->staticShadowMapRenderPass, attachments, this->_settings.shadowMapResolution, this->_settings.shadowMapResolution, 1 });
			this->staticShadowMapFramebuffers.push_back(fb);
		}
	}
}

void VulkanRenderer::renderShadowMaps(uint32_t frameIndex) {
	static const std::vector<vk::ClearValue> clearValues = { vk::ClearDepthStencilValue{1.0f} };
	static const std::array<glm::mat4, 6> views = {
		glm::rotate(glm::radians(90.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
		glm::rotate(glm::radians(-90.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
		glm::scale(glm::vec3{-1.0f, -1.0f, 1.0f}) * glm::rotate(glm::radians(-90.0f), glm::vec3{1.0f, 0.0f, 0.0f}),
		glm::scale(glm::vec3{-1.0f, -1.0f, 1.0f}) * glm::rotate(glm::radians(90.0f), glm::vec3{1.0f, 0.0f, 0.0f}),
		glm::rotate(glm::radians(180.0f), glm::vec3{0.0f, 1.0f, 0.0f}),
		glm::mat4{1.0f},
	};

	static const  glm::mat4 proj = glm::scale(glm::vec3{ -1.0f, -1.0f, 1.0f }) * glm::perspective<float>(glm::radians(90.0f), 1, 0.1f, 100.0f);

	vk::CommandBuffer& cb = this->shadowPassCommandBuffers[frameIndex];
	cb.reset();
	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

	cb.bindVertexBuffers(0, this->vertexIndexBuffer, { 0 });
	cb.bindIndexBuffer(this->vertexIndexBuffer, this->indexBufferOffset, vk::IndexType::eUint32);

	cb.bindPipeline(vk::PipelineBindPoint::eGraphics, this->shadowMapPipeline);

	for (auto&& [i, light] : iter::enumerate(this->lights)) {
		if (light.staticMapRendered)
			continue;

		const glm::vec3 cameraPos = light.point;

		std::vector<std::shared_ptr<Mesh>> sortedMeshes;
		for (const auto& el : iter::sorted(this->staticMeshes, [&cameraPos](const std::shared_ptr<Mesh>& a, const std::shared_ptr<Mesh>& b) { return glm::distance(a->barycenter(), cameraPos) < glm::distance(b->barycenter(), cameraPos); }))
			sortedMeshes.push_back(el);

		for (unsigned short j = 0; j < 6; j++) {
			cb.beginRenderPass(vk::RenderPassBeginInfo{ this->staticShadowMapRenderPass, this->staticShadowMapFramebuffers[i * 6 + j], vk::Rect2D{{0, 0}, {this->_settings.shadowMapResolution, this->_settings.shadowMapResolution}}, clearValues }, vk::SubpassContents::eInline);

			glm::mat4 view = views[j] * glm::translate(-cameraPos);
			glm::mat4 viewproj = proj * view;

			auto culledMeshes = iter::filter([viewproj](const std::shared_ptr<Mesh> mesh) {
				const glm::mat4 model = mesh->node->modelMatrix();
				const glm::mat4 modelviewproj = viewproj * model;
				return !cullMesh(*mesh, modelviewproj);
				}, sortedMeshes);


			for (auto& mesh : culledMeshes) {
				const glm::mat4 model = mesh->node->modelMatrix();
				const glm::mat4 modelviewproj = viewproj * model;

				std::vector<glm::mat4> pushConstants = { modelviewproj, model };

				cb.pushConstants<glm::mat4x4>(this->shadowMapPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstants);

				if (mesh->isIndexed)
					cb.drawIndexed(mesh->indices.size(), 1, mesh->firstIndex, 0, 0);
				else
					cb.draw(mesh->vertices.size(), 1, mesh->firstVertex, 0);
			}

			cb.endRenderPass();
			light.staticMapRendered = true;
		}
	}

	cb.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, this->graphicsQueueFamilyIndex, this->graphicsQueueFamilyIndex, this->shadowMapImage, vk::ImageSubresourceRange{ vk::ImageAspectFlagBits::eDepth, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS } });
	cb.blitImage(this->staticShadowMapImage, vk::ImageLayout::eTransferSrcOptimal, this->shadowMapImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageBlit{vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eDepth, 0, 0, static_cast<uint32_t>(this->lights.size() * 6) }, std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->_settings.shadowMapResolution), static_cast<int32_t>(this->_settings.shadowMapResolution), 1 } }, vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eDepth, 0, 0, static_cast<uint32_t>(this->lights.size() * 6) }, std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->_settings.shadowMapResolution), static_cast<int32_t>(this->_settings.shadowMapResolution), 1 } }}, vk::Filter::eNearest);

	for (const auto& [i, light] : iter::enumerate(this->lights)) {
		const glm::vec3 cameraPos = light.point;

		std::vector<std::shared_ptr<Mesh>> sortedMeshes;
		for (const auto& el : iter::sorted(this->dynamicMeshes, [&cameraPos](const std::shared_ptr<Mesh>& a, const std::shared_ptr<Mesh>& b) { return glm::distance(a->barycenter(), cameraPos) < glm::distance(b->barycenter(), cameraPos); }))
			sortedMeshes.push_back(el);

		for (unsigned short j = 0; j < 6; j++) {

			glm::mat4 view = views[j] * glm::translate(-cameraPos);
			glm::mat4 viewproj = proj * view;

			auto culledMeshes = iter::filter([viewproj](const std::shared_ptr<Mesh> mesh) {
				const glm::mat4 model = mesh->node->modelMatrix();
				const glm::mat4 modelviewproj = viewproj * model;
				return !cullMesh(*mesh, modelviewproj);
				}, sortedMeshes);

			uint32_t nonCulledMeshes = 0;
			for (auto& mesh : culledMeshes)
				nonCulledMeshes++;

			if (this->_settings.dynamicShadowsEnabled && nonCulledMeshes > 0) {
				cb.beginRenderPass(vk::RenderPassBeginInfo{ this->shadowMapRenderPass, this->shadowMapFramebuffers[i * 6 + j], vk::Rect2D{{0, 0}, {this->_settings.shadowMapResolution, this->_settings.shadowMapResolution}}, clearValues }, vk::SubpassContents::eInline);
				for (auto& mesh : culledMeshes) {
					const glm::mat4 model = mesh->node->modelMatrix();
					const glm::mat4 modelviewproj = viewproj * model;

					std::vector<glm::mat4> pushConstants = { modelviewproj, model };

					cb.pushConstants<glm::mat4x4>(this->shadowMapPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, pushConstants);

					if (mesh->isIndexed)
						cb.drawIndexed(mesh->indices.size(), 1, mesh->firstIndex, 0, 0);
					else
						cb.draw(mesh->vertices.size(), 1, mesh->firstVertex, 0);
				}
				cb.endRenderPass();
			}

			else {
				cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, this->graphicsQueueFamilyIndex, this->graphicsQueueFamilyIndex, this->shadowMapImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, static_cast<uint32_t>(i * 6 + j), 1} });
			}

		}
	}

	cb.end();

	this->graphicsQueue.submit(vk::SubmitInfo{ {}, {}, cb, this->shadowPassFinishedSemaphores[frameIndex] });
}