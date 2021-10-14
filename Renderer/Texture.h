#pragma once

#include <map>
#include <memory>

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.hpp>

class Texture {
	class TextureCore {
	public:
		~TextureCore();

	private:
		TextureCore() {};
		TextureCore(const TextureCore& rhs) = delete;

		std::string path;

		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t bytesPerChannel = 1;

		uint32_t maxMipLevels = UINT32_MAX;

		bool isLoadedToLocal = false;
		bool isLoadedToDevice = false;

		vk::Device device{};
		vma::Allocator allocator{};
		vk::Image _image{};
		vk::Format _imageFormat = vk::Format::eUndefined;
		vk::ImageView _imageView{};
		vma::Allocation _imageAllocation{};

		std::vector<unsigned char> data{};

		friend class Texture;
	};

public:
	Texture();
	Texture(const Texture& rhs) : path(rhs.path), core(rhs.core) {};
	//Texture(Texture&& rhs) noexcept;
	Texture(const std::string& path, vk::Format format = vk::Format::eUndefined);
	Texture(const std::string& path, uint32_t maxMipLevels, vk::Format format = vk::Format::eUndefined);

	void loadFromFile();
	void loadToDevice(const vk::Device device, const vma::Allocator allocator, const vk::Queue transferQueue, const vk::CommandPool commandPool);

	void destroy();

	vk::Image image() { return this->core->_image; };
	vk::Format imageFormat() { return this->core->_imageFormat; };
	vk::ImageView imageView() { return this->core->_imageView; };
	vma::Allocation imageAllocation() { return this->core->_imageAllocation; };

private:
	inline static std::map<std::string, std::shared_ptr<TextureCore>> textureCache{};
	std::string path;
	std::shared_ptr<TextureCore> core;
};

