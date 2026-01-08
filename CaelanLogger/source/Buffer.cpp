#include "Buffer.h"
#include <cstring>
Buffer::Buffer() :capacity(2000), size(0), remaining(capacity) {
	buffer = new char[capacity];
}
Buffer::Buffer(int capacity) : capacity(capacity), size(0), remaining(capacity) {
	buffer = new char[capacity];
}
Buffer::~Buffer() {
	delete[] buffer;
	buffer = nullptr;
}
bool Buffer::add(const char* src, int len) {
	if (len + size > capacity) return false;
	std::memcpy(&buffer[size], src, len);
	size += len;
	remaining -= len;
	return true;
}

bool Buffer::add(const char src) {
	if (1 + size > capacity) return false;
	buffer[size++] = src;
	remaining--;
	return true;
}

void Buffer::reset() {
	size = 0;
	remaining = capacity;
}