#pragma once

#include "Handle.h"

enum class ImageFormat {
	eR8G8B8Unorm,
	eR8G8B8Srgb,
	eR8G8B8A8Unorm,
	eR8G8B8A8Srgb,
	eR16G16B16Sfloat,
	eR16G16B16A16Sfloat,
	eR32G32B32Sfloat,
	eR32G32B32A32Sfloat,
};

typedef Handle<uint32_t, __COUNTER__> Image;
