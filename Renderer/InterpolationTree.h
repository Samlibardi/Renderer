#pragma once

#include <memory>
#include <tuple>
#include <utility>
#include <initializer_list>
#include <algorithm>
#include <vector>

template<typename key_t, typename elem_t> struct InterpolationTreeNode {
	InterpolationTreeNode(
		key_t key,
		elem_t value,
		std::shared_ptr<InterpolationTreeNode<key_t, elem_t>> prev = nullptr,
		std::shared_ptr<InterpolationTreeNode<key_t, elem_t>> next = nullptr
	) : key(key), value(value), prev(prev), next(next) {};

	key_t key;
	elem_t value;
	std::shared_ptr<InterpolationTreeNode<key_t, elem_t>> prev = nullptr;
	std::shared_ptr<InterpolationTreeNode<key_t, elem_t>> next = nullptr;
};

template<typename key_t, typename elem_t> class InterpolationTree {
public:
	InterpolationTree(const std::initializer_list<std::pair<key_t, elem_t>> elems);
	InterpolationTree() {};
	InterpolationTree(const InterpolationTree<key_t, elem_t>& other) {};
	InterpolationTree(InterpolationTree<key_t, elem_t>&& other) {};

	void push_back(std::pair<key_t, elem_t> val);
	//void push_back(key_t key, elem_t elem);
	//void clear();
	std::pair<std::pair<key_t, elem_t>, std::pair<key_t, elem_t>> at(key_t key);
private:
	std::unique_ptr<InterpolationTreeNode<key_t, elem_t>> root = nullptr;
	size_t size = 0;
};

template<typename key_t, typename elem_t>
std::unique_ptr<InterpolationTreeNode<key_t, elem_t>> buildSubtree(const typename std::vector<std::pair<key_t, elem_t>>::iterator begin, const typename std::vector<std::pair<key_t, elem_t>>::iterator end, const size_t size) {
	if (size == 0)
		return nullptr;
	else if (size == 1)
		return std::make_unique<InterpolationTreeNode<key_t, elem_t>>(begin->first, begin->second);
	else if (size == 2) {
		auto left = std::make_shared<InterpolationTreeNode<key_t, elem_t>>(begin->first, begin->second);
		return std::make_unique<InterpolationTreeNode<key_t, elem_t>>((begin + 1)->first, (begin + 1)->second, left);
	}
	else if (size == 3) {
		auto left = std::make_shared<InterpolationTreeNode<key_t, elem_t>>(begin->first, begin->second);
		auto right = std::make_shared<InterpolationTreeNode<key_t, elem_t>>((begin + 2)->first, (begin + 2)->second);
		return std::make_unique<InterpolationTreeNode<key_t, elem_t>>((begin + 1)->first, (begin + 1)->second, left, right);
	}
	else {
		typename std::vector<std::pair<key_t, elem_t>>::iterator midpoint = begin + size / 2;
		std::shared_ptr<InterpolationTreeNode<key_t, elem_t>> left = buildSubtree<key_t, elem_t>(begin, midpoint, 1 + size / 2);
		std::shared_ptr<InterpolationTreeNode<key_t, elem_t>> right = buildSubtree<key_t, elem_t>(midpoint + 1, end, size - (1 + size / 2));
		return std::make_unique<InterpolationTreeNode<key_t, elem_t>>(midpoint->first, midpoint->second, left, right);
	}
}

template<typename key_t, typename elem_t>
InterpolationTree<key_t, elem_t>::InterpolationTree(std::initializer_list<std::pair<key_t, elem_t>> elems)
{
	std::vector<std::pair<key_t, elem_t>> elemsVec = elems;
	std::sort(elemsVec.begin(), elemsVec.end(), [](std::pair<key_t, elem_t> a, std::pair<key_t, elem_t> b) { return a.first < b.first; });
	this->root = buildSubtree<key_t, elem_t>(elemsVec.begin(), elemsVec.end(), elemsVec.size());
	this->size = elemsVec.size();
}

template<typename key_t, typename elem_t>
std::pair<std::pair<key_t, elem_t>, std::pair<key_t, elem_t>> InterpolationTree<key_t, elem_t>::at(key_t key) {
	InterpolationTreeNode<key_t, elem_t>* node = this->root.get();
	InterpolationTreeNode<key_t, elem_t>* closestHigher = nullptr;
	InterpolationTreeNode<key_t, elem_t>* closestLower = nullptr;

	while (true) {
		if (node == nullptr)
			break;

		if (node->key < key) {
			closestLower = node;
			node = node->next.get();
			continue;
		}

		if (node->key >= key) {
			closestHigher = node;
			node = node->prev.get();
			continue;
		}
	}

	if (closestHigher == nullptr && closestLower != nullptr)
		return { {closestLower->key, closestLower->value}, {closestLower->key, closestLower->value} };
	if (closestLower == nullptr && closestHigher != nullptr)
		return { {closestHigher->key, closestHigher->value}, {closestHigher->key, closestHigher->value} };

	return { {closestLower->key, closestLower->value}, {closestHigher->key, closestHigher->value} };
}