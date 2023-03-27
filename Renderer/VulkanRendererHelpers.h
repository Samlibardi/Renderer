#pragma once

#include <vulkan/vulkan.hpp>

#include "Mesh.h"
#include "Image.h"

constexpr vk::IndexType vkIndexTypeFromAttributeValueType(AttributeValueType valueType) {
	switch (valueType) {
	case AttributeValueType::eUint8:
		return vk::IndexType::eUint8EXT;
	case AttributeValueType::eUint32:
		return vk::IndexType::eUint32;
	case AttributeValueType::eUint16:
		return vk::IndexType::eUint16;
	default:
		return vk::IndexType::eNoneKHR;
	}
}

constexpr vk::Format vkFormatFromImageFormat(const ImageFormat imageFormat) {
	switch (imageFormat) {
		case ImageFormat::eR8G8B8Unorm:
			return vk::Format::eR8G8B8Unorm;
		case ImageFormat::eR8G8B8Srgb:
			return vk::Format::eR8G8B8Srgb;
		case ImageFormat::eR8G8B8A8Unorm:
			return vk::Format::eR8G8B8A8Unorm;
		case ImageFormat::eR8G8B8A8Srgb:
			return vk::Format::eR8G8B8A8Srgb;
		case ImageFormat::eR16G16B16Sfloat:
			return vk::Format::eR16G16B16Sfloat;
		case ImageFormat::eR16G16B16A16Sfloat:
			return vk::Format::eR16G16B16A16Sfloat;
		case ImageFormat::eR32G32B32Sfloat:
			return vk::Format::eR32G32B32Sfloat;
		case ImageFormat::eR32G32B32A32Sfloat:
			return vk::Format::eR32G32B32A32Sfloat;
		default:
			return vk::Format::eUndefined;
	}
}