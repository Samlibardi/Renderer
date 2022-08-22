#include "Buffer.h"
#include <fstream>

Buffer::Buffer(std::string path) {
	this->fileSrc = path;
	std::ifstream file(this->fileSrc, std::ios::binary | std::ios::ate);
	file.seekg(0, std::ios::beg);
	this->len = file.tellg();
}

Buffer::Buffer(char* ptr, size_t len) {
	std::allocator<char> allocator;
	char* newPtr = allocator.allocate(len);
	std::memcpy(newPtr, ptr, len * sizeof(char));
	this->memSrc = std::shared_ptr<char>(newPtr);
}

size_t Buffer::byteLength() const {
	return this->len;
}

void Buffer::read(char* dest) const {
	if (!this->fileSrc.empty()) {
		std::ifstream file(this->fileSrc, std::ios::binary | std::ios::ate);
		file.seekg(0, std::ios::beg);
		
		file.read(dest, this->len);
	}

	if (this->memSrc != nullptr) {
		memcpy(dest, this->memSrc.get(), this->len);
	}
}
