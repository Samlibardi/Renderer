#include "Texture.h"

#include <stb_image.h>
#include <vector>

TextureCore::~TextureCore() {
	if (this->isLoadedToLocal) {
		this->data = std::vector<unsigned char>();
		this->isLoadedToLocal = false;
	}
	if (this->isLoadedToDevice) {
		this->device.destroyImageView(this->_imageView);
		this->allocator.destroyImage(this->_image, this->_imageAllocation);
		this->isLoadedToDevice = false;
	}
}

Texture::Texture(const std::string& path) : path(path) {
	if(Texture::textureCache.count(path)){
		this->core = Texture::textureCache[path];
		return;
	}
	else {
		this->core = std::shared_ptr<TextureCore>(new TextureCore());
		this->core->path = path;
		Texture::textureCache[path] = this->core;
		return;
	}
}

Texture::Texture() : Texture("./textures/clear.png") {}
Texture::Texture(const std::string& path, const uint32_t maxMipLevels) : Texture(path) {
	this->core->maxMipLevels = maxMipLevels;
}

void Texture::loadFromFile() {
	if (this->core->isLoadedToLocal)
		return;

	int w, h, n, bpc = 1;
	unsigned char* data;
	if (stbi_is_hdr(this->path.data())) {
		data = reinterpret_cast<unsigned char*>(stbi_loadf(this->path.data(), &w, &h, &n, 4));
		bpc = 4;
	}
	else
		data = stbi_load(this->path.data(), &w, &h, &n, 4);

	size_t len = static_cast<size_t>(w) * h * 4 * bpc;

	this->core->data.resize(len);
	memcpy(this->core->data.data(), data, len);

	stbi_image_free(data);

	this->core->width = w;
	this->core->height = h;
	this->core->bytesPerChannel = bpc;

	this->core->isLoadedToLocal = true;
}

void Texture::loadToDevice(const vk::Device device, const vma::Allocator allocator, const vk::Queue transferQueue, const vk::CommandPool commandPool) {
	if (this->core->isLoadedToDevice)
		return;

	if (!this->core->isLoadedToLocal)
		this->loadFromFile();

	auto[stagingBuffer, stagingBufferAllocation] = allocator.createBuffer(vk::BufferCreateInfo{ {}, this->core->data.size(), vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive }, vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eCpuToGpu });
	void* sbData = allocator.mapMemory(stagingBufferAllocation);
	memcpy(sbData, this->core->data.data(), this->core->data.size());
	allocator.unmapMemory(stagingBufferAllocation);
	
	uint32_t mipLevels = 1u;
	uint32_t maxDim = std::max(this->core->width, this->core->height);
	if (maxDim > 1u) mipLevels = std::floor(std::log2(maxDim)) + 1;

	mipLevels = std::min(mipLevels, this->core->maxMipLevels);

	this->core->_imageFormat = vk::Format::eR8G8B8A8Unorm;
	if (this->core->bytesPerChannel == 4u)
		this->core->_imageFormat = vk::Format::eR32G32B32A32Sfloat;

	std::tie(this->core->_image, this->core->_imageAllocation) = allocator.createImage(
		vk::ImageCreateInfo{ {}, vk::ImageType::e2D, this->core->_imageFormat, vk::Extent3D{this->core->width, this->core->height, 1}, mipLevels, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::SharingMode::eExclusive },
		vma::AllocationCreateInfo{ {}, vma::MemoryUsage::eGpuOnly }
	);

	this->core->_imageView = device.createImageView(vk::ImageViewCreateInfo{ {}, this->core->_image, vk::ImageViewType::e2D, this->core->_imageFormat, vk::ComponentMapping{}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1 } });

	auto cmdBuffers = device.allocateCommandBuffers(vk::CommandBufferAllocateInfo{ commandPool, vk::CommandBufferLevel::ePrimary, 1 });
	vk::CommandBuffer cb = cmdBuffers[0];

	cb.begin(vk::CommandBufferBeginInfo{ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
	cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{ {}, vk::AccessFlagBits::eTransferWrite, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->core->_image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1} });
	std::vector<vk::BufferImageCopy> copyRegions = { vk::BufferImageCopy{0, 0, 0, vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {0, 0, 0}, {this->core->width, this->core->height, 1}  } };
	cb.copyBufferToImage(stagingBuffer, this->core->_image, vk::ImageLayout::eTransferDstOptimal, copyRegions);
	
	if (mipLevels > 1) {
		//transition base miplevel
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->core->_image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });

		std::vector<vk::ImageBlit> transferRegions{};
		for (uint32_t i = 1; i < mipLevels; i++) {
			std::array<vk::Offset3D, 2> srcOffsets = { vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->core->width), static_cast<int32_t>(this->core->height), 1 } };
			std::array<vk::Offset3D, 2> dstOffsets = { vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(this->core->width >> i), static_cast<int32_t>(this->core->height >> i), 1 } };
			transferRegions.push_back(vk::ImageBlit{ vk::ImageSubresourceLayers{{vk::ImageAspectFlagBits::eColor}, 0, 0, 1}, srcOffsets, vk::ImageSubresourceLayers{ {vk::ImageAspectFlagBits::eColor}, i, 0, 1 }, dstOffsets });
		}
		cb.blitImage(this->core->_image, vk::ImageLayout::eTransferSrcOptimal, this->core->_image, vk::ImageLayout::eTransferDstOptimal, transferRegions, vk::Filter::eLinear);
		//transition base miplevel
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->core->_image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
		//transition remaining miplevels
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->core->_image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 1, mipLevels - 1, 0, 1} });
	}
	else
		cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, vk::ImageMemoryBarrier{ vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, this->core->_image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1} });
	
	cb.end();

	vk::Fence fence = device.createFence(vk::FenceCreateInfo{});
	transferQueue.submit(vk::SubmitInfo{ {}, {}, cb, {} }, fence);
	device.waitForFences(fence, true, UINT64_MAX);
	allocator.destroyBuffer(stagingBuffer, stagingBufferAllocation);
	device.freeCommandBuffers(commandPool, cb);
	device.destroyFence(fence);
	this->core->data = std::vector<unsigned char>();

	this->core->isLoadedToDevice = true;
	this->core->isLoadedToLocal = false;
}

void Texture::destroy() {
	this->core->~TextureCore();
}