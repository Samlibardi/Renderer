#pragma once

#include <glm/vec3.hpp>
#include "Flags.h"

enum class PointLightFlagBits : uint8_t {
	eNone = 0,
	eCastShadows = 1 << 0,
	eHasShadowMap = 1 << 1,
	eStaticShadowMapRendered = 1 << 2,
};
using PointLightFlags = typename Flags<PointLightFlagBits>;

constexpr PointLightFlags operator| (const PointLightFlagBits& lhs, const PointLightFlagBits& rhs) noexcept { return static_cast<PointLightFlags>(static_cast<PointLightFlags>(lhs) | rhs); }
constexpr PointLightFlags operator& (const PointLightFlagBits& lhs, const PointLightFlagBits& rhs) noexcept { return static_cast<PointLightFlags>(static_cast<PointLightFlags>(lhs) & rhs); }
constexpr PointLightFlags operator^ (const PointLightFlagBits& lhs, const PointLightFlagBits& rhs) noexcept { return static_cast<PointLightFlags>(static_cast<PointLightFlags>(lhs) ^ rhs); }
constexpr PointLightFlagBits operator~ (const PointLightFlagBits& rhs) noexcept { return static_cast<PointLightFlagBits>(~static_cast<uint8_t>(rhs)); }

class PointLight {
public:
	glm::vec3 point;
	glm::vec3 intensity;

	PointLightFlags flags;
	uint16_t shadowMapIndex;

	PointLight(glm::vec3 point, glm::vec3 intensity, bool castShadows) : point(point), intensity(intensity), flags(PointLightFlags{} | (castShadows ? PointLightFlagBits::eCastShadows : PointLightFlagBits::eNone)), shadowMapIndex(~0U) {}
};
