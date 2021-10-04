#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

class Texture
{
public:
	void loadFromFile();
	void loadToDevice(const vk::Device device, const vma::Allocator allocator, const vk::Queue transferQueue, const vk::CommandPool commandPool);

	void destroy(const vk::Device device, const vma::Allocator allocator);

	Texture() {};
	Texture(std::string path) : path(path) {};
	Texture(std::string path, uint32_t maxMipLevels) : path(path), maxMipLevels(maxMipLevels) {};
	Texture(std::vector<unsigned char> bytes, uint32_t width, uint32_t height, uint32_t bytesPerChannel) : data(bytes), width(width), height(height), bytesPerChannel(bytesPerChannel), isLoadedToLocal(true) {};

	vk::Image image() { return this->_image; };
	vk::Format imageFormat() { return this->_imageFormat; };
	vk::ImageView imageView() { return this->_imageView; };
	vma::Allocation imageAllocation() { return this->_imageAllocation; };

private:
	std::string path;

	std::vector<unsigned char> data{};

	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t bytesPerChannel = 1;

	uint32_t maxMipLevels = UINT32_MAX;

	bool isLoadedToLocal = false;
	bool isLoadedToDevice = false;

	vk::Image _image{};
	vk::Format _imageFormat = vk::Format::eR8G8B8A8Unorm;
	vk::ImageView _imageView{};
	vma::Allocation _imageAllocation{};
	
};

