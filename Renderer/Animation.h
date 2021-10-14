#pragma once
#include <stdexcept>

#include <glm/common.hpp>
#include <glm/gtx/spline.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cppitertools/enumerate.hpp>

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
	) : keyframeTimes(keyframeTimes), keyframes(keyframeValues), interpolationCurve(interpolation), repeatMode(repeat) {
		assert(keyframeTimes.size() > 0);
		assert(keyframeValues.size() == keyframeTimes.size());

		std::vector<std::pair<float, size_t>> keyframeIndexes{};
		for (auto&& [i, t] : iter::enumerate(keyframeTimes) ) {
			keyframeIndexes.emplace_back(std::pair{ t, i });
		}

		this->keyframeIndexTree = std::make_unique<InterpolationTree<float, size_t>>(keyframeIndexes.begin(), keyframeIndexes.end());
	};
	Animation(const Animation<T>& other) : 
		keyframeTimes(other.keyframeTimes),
		keyframes(other.keyframes),
		keyframeIndexTree(std::make_unique<InterpolationTree<float, size_t>>(*other.keyframeIndexTree)),
		interpolationCurve(other.interpolationCurve),
		repeatMode(other.repeatMode) {};
	Animation(Animation<T>&& other) :
		keyframeTimes(std::move(other.keyframeTimes)),
		keyframes(std::move(other.keyframes)),
		keyframeIndexTree(std::move(other.keyframeIndexTree)),
		interpolationCurve(other.interpolationCurve),
		repeatMode(other.repeatMode) {};

	T valueAt(float t);
private:
	AnimationInterpolationCurve interpolationCurve = AnimationInterpolationCurve::eLinear;
	AnimationRepeatMode repeatMode = AnimationRepeatMode::eClamp;
	std::unique_ptr<InterpolationTree<float, size_t>> keyframeIndexTree;
	std::vector<float> keyframeTimes;
	std::vector<T> keyframes;
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

	auto [kf0, kf1] = this->keyframeIndexTree->at(t);
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
