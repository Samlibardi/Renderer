#include "Texture.h"

#include <stb_image.h>
#include <vector>

void Texture::loadFromFile() {
	if (this->path.empty()) {
		this->data = { 0xff, 0xff, 0xff, 0xff };
		this->width = 1;
		this->height = 1;
		this->bytesPerChannel = 1;
		this->isLoadedToLocal = true;
		return;
	}

	int w, h, n, bpc = 1;
	unsigned char* data;
	if (stbi_is_hdr(this->path.data())) {
		data = reinterpret_cast<unsigned char*>(stbi_loadf(this->path.data(), &w, &h, &n, 4));
		bpc = 4;
	}
	else
		data = stbi_load(this->path.data(), &w, &h, &n, 4);

	size_t len = static_cast<size_t>(w) * h * 4 * bpc;

	this->data.resize(len);
	memcpy(this->data.data(), data, len);

	stbi_image_free(data);

	this->width = w;
	this->height = h;
	this->bytesPerChannel = bpc;

	this->isLoadedToLocal = true;
}

void Texture::loadToDevice(const vk::Device device, const vma::Allocator allocator, const vk::Queue transferQueue, const vk::CommandPool commandPool) {
	if (!this->isLoadedToLocal)
		this->loadFromFile();

	auto[stagingBuffer, stagingBufferAllocation] = allocator.createBuffer(vk::BufferCreateInfo{ {}, this->data.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuToGpu });
	void* sbData = allocator.mapMemory(stagingBufferAllocation);
	memcpy(sbData, this->data.data(), this->data.size());
	allocator.unmapMemory(stagingBufferAllocation);
	
	uint32_t mipLevels = 1u;
	uint32_t maxDim = std::max(this->width, this->height);
	if (maxDim > 1u) mipLevels = std::floor(std::log2(maxDim)) + 1;

	mipLevels = std::min(mipLevels, this->maxMipLevels);

	this->_imageFormat = vk::Format::eR8G8B8A8Unorm;
	if (this->bytesPerChannel == 4u)
		this->_imageFormat = vk::Format::eR32G32B32A32Sfloat;

	std::tie(this->_image, this->_imageAllocation) = allocator.createImage(
		vk::ImageCreateInfo{ {}, vk::ImageType::e2D, this->_imageFormat, vk::Extent3D{this->width, this->height, 1}, mipLevels, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	this->_imageView = device.createImageView(vk::ImageViewCreateInfo{ {}, _image, vk::ImageViewType::e2D, this->_imageFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1 } });

	auto cmdBuffers = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ commandPool, vk::CommandBufferLevel::ePrimary, 1 });
	vk::CommandBuffer cb = cmdBuffers[0];

	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{ {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->_image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1} });
	std::vector<vk::BufferImageCopy> copyRegions = { vk::BufferImageCopy{0, 0, 0, vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0}, {this->width, this->height, 1}  } };
	cb.copyBufferToImage(stagingBuffer, this->_image, vk::ImageLayout::eTransferDstOptimal, copyRegions);
	
	if (mipLevels > 1) {
		//transition base miplevel
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->_image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });

		std::vector<vk::ImageBlit> transferRegions{};
		for (uint32_t i = 1; i < mipLevels; i++) {
			std::array<vk::Offset3D, 2> srcOffsets = { vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->width), static_cast<int32_t>(this->height), 1 } };
			std::array<vk::Offset3D, 2> dstOffsets = { vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->width >> i), static_cast<int32_t>(this->height >> i), 1 } };
			transferRegions.push_back(vk::ImageBlit{ vk::ImageSubresourceLayers{{vk::ImageAspectFlagBits::eColor}, 0, 0, 1}, srcOffsets, vk::ImageSubresourceLayers{ {vk::ImageAspectFlagBits::eColor}, i, 0, 1 }, dstOffsets });
		}
		cb.blitImage(this->_image, vk::ImageLayout::eTransferSrcOptimal, this->_image, vk::ImageLayout::eTransferDstOptimal, transferRegions, vk::Filter::eLinear);
		//transition base miplevel
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->_image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
		//transition remaining miplevels
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->_image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 1, mipLevels - 1, 0, 1} });
	}
	else
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->_image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
	
	cb.end();

	vk::Fence fence = device.createFence(vk::FenceCreateInfo{});
	transferQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
	device.waitForFences(fence, true, UINT64_MAX);
	allocator.destroyBuffer(stagingBuffer, stagingBufferAllocation);
	device.freeCommandBuffers(commandPool, cb);
	device.destroyFence(fence);
	this->data.clear();

	this->isLoadedToDevice = true;
	this->isLoadedToLocal = false;
}

void Texture::destroy(const vk::Device device, const vma::Allocator allocator) {
	if (this->isLoadedToLocal) {
		this->data.clear();
	}
	if (this->isLoadedToDevice) {
		device.destroyImageView(this->_imageView);
		allocator.destroyImage(this->_image, this->_imageAllocation);
	}
}