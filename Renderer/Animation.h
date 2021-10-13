#pragma once
#include <stdexcept>

#include <glm/common.hpp>
#include <glm/gtx/spline.hpp>
#include "InterpolationTree.h"

enum class AnimationInterpolationCurve {
	eLinear,
	eStep,
	eCubicSpline,
};

enum class AnimationRepeatMode {
	eClamp,
	eRepeat,
	eMirror,
};

template<typename T>
class Animation
{
public:
	Animation(
		std::vector<float> keyframeTimes,
		std::vector<T> keyframeValues,
		AnimationInterpolationCurve interpolation = AnimationInterpolationCurve::eLinear,
		AnimationRepeatMode repeat = AnimationRepeatMode::eClamp
	) : keyframeTimes(keyframeTimes), keyframes(keyframeValues), interpolationCurve(interpolation), repeatMode(repeat) {};

	T valueAt(float t);
private:
	AnimationInterpolationCurve interpolationCurve = AnimationInterpolationCurve::eLinear;
	AnimationRepeatMode repeatMode = AnimationRepeatMode::eClamp;
	InterpolationTree<float, size_t> keyframeIndexTree{};
	std::vector<float> keyframeTimes{};
	std::vector<T> keyframes{};
};

template<typename T>
T Animation<T>::valueAt(float t)
{
	if (this->keyframes.size() == 1)
		return this->keyframes.front();

	switch (repeatMode) {
	case AnimationRepeatMode::eClamp:
		t = std::clamp(t, this->keyframeTimes.front(), this->keyframeTimes.back());
		break;
	case AnimationRepeatMode::eRepeat:
		t = std::fmodf(t, this->keyframeTimes.back()) + this->keyframeTimes.front();
		break;
	case AnimationRepeatMode::eMirror:
		float nloops;
		t = std::modff(t/this->keyframeTimes.back(), &nloops);
		t = (static_cast<int>(nloops) % 2) ? this->keyframeTimes.back() - t : t;
	}

	auto [kf0, kf1] = this->keyframeIndexTree.at(t);
	auto [t0, i0] = kf0;
	auto [t1, i1] = kf1;

	switch (this->interpolationCurve) {
	case AnimationInterpolationCurve::eStep:
	case AnimationInterpolationCurve::eCubicSpline:
		return (t - t0 < t1 - t) ? this->keyframes[i0] : this->keyframes[i1];
		break;
	case AnimationInterpolationCurve::eLinear:
		return glm::mix(this->keyframes[i0], this->keyframes[i1], t/(t1 - t0));
		break;
	}
}
