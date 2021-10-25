#include <cppitertools/enumerate.hpp>

#include "VulkanRenderer.h"

void VulkanRenderer::createTonemapPipeline() {
	std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
		vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
		vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
		vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
		vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
	};
	this->tonemapDescriptorSetLayout = this->device.createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{ {}, setLayoutBindings });
	std::vector<vk::DescriptorSetLayout> allocateDescriptorSetLayouts{};
	for (auto iv : this->swapchainImageViews) {
		allocateDescriptorSetLayouts.push_back(this->tonemapDescriptorSetLayout);
	}
	this->tonemapDescriptorSets = this->device.allocateDescriptorSets(vk::DescriptorSetAllocateInfo{ this->descriptorPool, allocateDescriptorSetLayouts });

	vk::PushConstantRange pushConstantRange{ vk::ShaderStageFlagBits::eCompute, 0, sizeof(float) * 2 };
	this->tonemapPipelineLayout = this->device.createPipelineLayout(vk::PipelineLayoutCreateInfo{ {}, this->tonemapDescriptorSetLayout, pushConstantRange });

	vk::ShaderModule computeModule = loadShader("./shaders/tonemap.comp.spv");
	vk::PipelineShaderStageCreateInfo computeStageInfo = vk::PipelineShaderStageCreateInfo{ {}, vk::ShaderStageFlagBits::eCompute, computeModule, "main" };

	vk::Result r;
	std::tie(r, this->tonemapPipeline) = this->device.createComputePipeline(this->pipelineCache, vk::ComputePipelineCreateInfo{ {}, computeStageInfo, this->tonemapPipelineLayout});

	this->tonemapSampler = this->device.createSampler(vk::SamplerCreateInfo{ {}, vk::Filter::eNearest, vk::Filter::eNearest, vk::SamplerMipmapMode::eNearest, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, vk::SamplerAddressMode::eClampToEdge, {}, false, {}, false, {}, {}, {}, {}, true });

	for (auto [i, iv] : iter::enumerate(this->swapchainImageViews)) {
		vk::DescriptorImageInfo colorImageInfo{ this->tonemapSampler, this->colorImageView, vk::ImageLayout::eShaderReadOnlyOptimal };
		vk::DescriptorImageInfo bloomImageInfo{ this->tonemapSampler, this->bloomImageViews[0], vk::ImageLayout::eShaderReadOnlyOptimal };
		vk::DescriptorImageInfo outImageInfo{ vk::Sampler{}, iv, vk::ImageLayout::eGeneral };
		vk::DescriptorImageInfo outLogLumImageInfo{ vk::Sampler{}, this->luminanceImageView, vk::ImageLayout::eGeneral };
		std::vector<vk::WriteDescriptorSet> writeDescriptorSets = {
			vk::WriteDescriptorSet{ this->tonemapDescriptorSets[i], 0, 0, vk::DescriptorType::eCombinedImageSampler, colorImageInfo },
			vk::WriteDescriptorSet{ this->tonemapDescriptorSets[i], 1, 0, vk::DescriptorType::eCombinedImageSampler, bloomImageInfo },
			vk::WriteDescriptorSet{ this->tonemapDescriptorSets[i], 2, 0, vk::DescriptorType::eStorageImage, outImageInfo },
			vk::WriteDescriptorSet{ this->tonemapDescriptorSets[i], 3, 0, vk::DescriptorType::eStorageImage, outLogLumImageInfo },
		};
		this->device.updateDescriptorSets(writeDescriptorSets, {});
	}
}