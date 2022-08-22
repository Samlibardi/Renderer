#pragma once
#include <map>
#include <string>
#include <memory>

class Buffer
{
	std::string fileSrc = {};
	std::shared_ptr<char> memSrc = nullptr;
	size_t len = 0;

public:
	explicit Buffer(std::string path);
	explicit Buffer(char* ptr, size_t len);
	Buffer(const Buffer& other) noexcept = default;
	Buffer(Buffer&& other) noexcept {
		this->memSrc = std::move(other.memSrc);
		this->len = other.len;
		this->fileSrc = other.fileSrc;
	}
	size_t byteLength() const;
	void read(char* dest) const;

	bool operator== (const Buffer& rhs) {
		if (this->memSrc != nullptr && rhs.memSrc != nullptr && this->memSrc == rhs.memSrc) return true;
		if (!this->fileSrc.empty() && !rhs.fileSrc.empty() && this->fileSrc == rhs.fileSrc) return true;
		if (this->memSrc == nullptr && rhs.memSrc == nullptr && this->fileSrc.empty() && rhs.fileSrc.empty()) return true;
		return false;
	}

friend struct std::hash<Buffer>;
};

template<>
struct std::hash<Buffer>
{
	std::size_t operator()(Buffer const& buffer) const noexcept
	{
		if (buffer.memSrc != nullptr)
			return std::hash<char*>{}(buffer.memSrc.get());
		if (!buffer.fileSrc.empty())
			return std::hash<std::string>{}(buffer.fileSrc);
		return std::hash<std::size_t>{}(0u);
	}
};
