#include "VulkanRenderer.h"

void VulkanRenderer::createBloomPipelines() {
	std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
			vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
			vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
	};
	this->bloomDescriptorSetLayout = this->device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, setLayoutBindings });
	std::vector<vk::DescriptorSetLayout> allocateDescriptorSetLayouts{};
	for (uint32_t i = 0; i < this->bloomMipLevels - 1; i++) {
		allocateDescriptorSetLayouts.push_back(this->bloomDescriptorSetLayout);
	}
	this->bloomDownsampleDescriptorSets = this->device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ this->descriptorPool, allocateDescriptorSetLayouts });
	this->bloomUpsampleDescriptorSets = this->device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ this->descriptorPool, allocateDescriptorSetLayouts });

	this->bloomPipelineLayout = this->device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, this->bloomDescriptorSetLayout, {} });

	vk::ShaderModule downsampleModule = loadShader("./shaders/bloomDownsample.comp.spv");
	vk::PipelineShaderStageCreateInfo downsampleStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eCompute, downsampleModule, "main" };

	vk::Result r;
	std::tie(r, this->bloomDownsamplePipeline) = this->device.createComputePipeline(this->pipelineCache, vk::ComputePipelineCreateInfo{ {}, downsampleStageInfo, this->bloomPipelineLayout});

	vk::ShaderModule upsampleModule = loadShader("./shaders/bloomUpsample.comp.spv");
	vk::PipelineShaderStageCreateInfo upsampleStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eCompute, upsampleModule, "main" };
	
	std::tie(r, this->bloomUpsamplePipeline) = this->device.createComputePipeline(this->pipelineCache, vk::ComputePipelineCreateInfo{ {}, upsampleStageInfo, this->bloomPipelineLayout});
}

void VulkanRenderer::createBloomImage() {
	std::tie(this->bloomImage, this->bloomImageAllocation) = this->allocator.createImage(vk::ImageCreateInfo{ {}, vk::ImageType::e2D, this->bloomAttachmentFormat, vk::Extent3D{this->swapchainExtent, 1 }, this->bloomMipLevels, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive, this->graphicsQueueFamilyIndex }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly });

	this->bloomDownsampleSampler = this->device.createSampler(vk::SamplerCreateInfo{ {}, vk::Filter::eLinear, vk::Filter::eLinear, vk::SamplerMipmapMode::eNearest, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, {}, false, {}, false, {}, {}, {}, {}, true });
	this->bloomUpsampleSampler = this->device.createSampler(vk::SamplerCreateInfo{ {}, vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, {}, false, {}, false, {}, {}, {}, {}, true });

	for (uint32_t i = 0; i < this->bloomMipLevels; i++) {
		vk::ImageView iv = this->device.createImageView(vk::ImageViewCreateInfo{ {}, this->bloomImage, vk::ImageViewType::e2D, this->bloomAttachmentFormat, {}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, i, 1, 0, 1} });
		this->bloomImageViews.push_back(iv);
	}

	for (uint32_t i = 0; i < this->bloomMipLevels - 1; i++) {
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets{};
		vk::DescriptorImageInfo inputImageInfo;

		inputImageInfo = { this->bloomDownsampleSampler, this->bloomImageViews[i], vk::ImageLayout::eShaderReadOnlyOptimal };
		writeDescriptorSets.push_back(vk::WriteDescriptorSet{ this->bloomDownsampleDescriptorSets[i], 0, 0, vk::DescriptorType::eCombinedImageSampler, inputImageInfo });
		vk::DescriptorImageInfo outputImageInfo{ {}, this->bloomImageViews[i + 1], vk::ImageLayout::eGeneral };
		writeDescriptorSets.push_back(vk::WriteDescriptorSet{ this->bloomDownsampleDescriptorSets[i], 1, 0, vk::DescriptorType::eStorageImage, outputImageInfo });
		this->device.updateDescriptorSets(writeDescriptorSets, {});
	}

	for (uint32_t i = 0; i < this->bloomMipLevels - 1; i++) {
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets{};
		vk::DescriptorImageInfo inputImageInfo{ this->bloomUpsampleSampler, this->bloomImageViews[this->bloomMipLevels - 1 - i], vk::ImageLayout::eShaderReadOnlyOptimal };
		writeDescriptorSets.push_back(vk::WriteDescriptorSet{ this->bloomUpsampleDescriptorSets[i], 0, 0, vk::DescriptorType::eCombinedImageSampler, inputImageInfo });
		vk::DescriptorImageInfo outputImageInfo{ {}, this->bloomImageViews[this->bloomMipLevels - 1 - i - 1], vk::ImageLayout::eGeneral };
		writeDescriptorSets.push_back(vk::WriteDescriptorSet{ this->bloomUpsampleDescriptorSets[i], 1, 0, vk::DescriptorType::eStorageImage, outputImageInfo });
		this->device.updateDescriptorSets(writeDescriptorSets, {});
	}
}

void VulkanRenderer::recordBloomCommandBuffers() {
	auto buffers = this->device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ this->commandPool, vk::CommandBufferLevel::ePrimary, FRAMES_IN_FLIGHT });
	for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
		vk::CommandBuffer cb = buffers[i];

		cb.begin(vk::CommandBufferBeginInfo{});

		cb.bindPipeline(vk::PipelineBindPoint::eCompute, this->bloomDownsamplePipeline);

		cb.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->colorImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->bloomImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
		cb.blitImage(this->colorImage, vk::ImageLayout::eTransferSrcOptimal, this->bloomImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageBlit{ vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->swapchainExtent.width), static_cast<int32_t>(this->swapchainExtent.height), 1 } }, vk::ImageSubresourceLayers{ vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->swapchainExtent.width), static_cast<int32_t>(this->swapchainExtent.height), 1 } } }, vk::Filter::eNearest);
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->bloomImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });

		for (uint32_t i = 0; i < this->bloomMipLevels - 1; i++) {
			if(i > 0)
				cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->bloomImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, i, 1, 0, 1} });
			cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eShaderWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->bloomImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, i + 1, 1, 0, 1} });
			
			cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, this->bloomPipelineLayout, 0, this->bloomDownsampleDescriptorSets[i], {});
			uint32_t w = this->swapchainExtent.width >> (i + 1), h = this->swapchainExtent.height >> (i + 1);
			cb.dispatch(w % 16 ? w / 16 + 1 : w / 16, h % 16 ? h / 16 + 1 : h / 16, 1);
		}

		cb.bindPipeline(vk::PipelineBindPoint::eCompute, this->bloomUpsamplePipeline);

		for (uint32_t i = 0; i < this->bloomMipLevels - 1; i++) {
			cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eGeneral, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->bloomImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, this->bloomMipLevels - 1 - i, 1, 0, 1} });
			cb.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader, vk::DependencyFlagBits::eByRegion, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eShaderRead, vk::AccessFlagBits::eShaderWrite, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eGeneral, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->bloomImage, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, this->bloomMipLevels - 1 - i - 1, 1, 0, 1} });

			cb.bindDescriptorSets(vk::PipelineBindPoint::eCompute, this->bloomPipelineLayout, 0, this->bloomUpsampleDescriptorSets[i], {});
			uint32_t w = this->swapchainExtent.width >> (this->bloomMipLevels - 1 - i - 1), h = this->swapchainExtent.height >> (this->bloomMipLevels - 1 - i - 1);
			cb.dispatch(w % 16 ? w / 16 + 1 : w / 16, h % 16 ? h / 16 + 1 : h / 16, 1);
		}

		cb.end();
		this->bloomCommandBuffers[i] = cb;
	}
}