#include "Buffer.h"
#include <cstring>
Buffer::Buffer() :capacity(2000), size(0) {
	buffer = new char[capacity];
}
Buffer::Buffer(int capacity) : capacity(capacity), size(0) {
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
	return true;
}

bool Buffer::add(const char src) {
	if (1 + size > capacity) return false;
	buffer[size++] = src;
	return true;
}

int Buffer::getSize()
{
	return size;
}

int Buffer::getCapacity()
{
	return capacity;
}

char* Buffer::getBuffer()
{
	return buffer;
}

void Buffer::reset() 
{
	size = 0;
}