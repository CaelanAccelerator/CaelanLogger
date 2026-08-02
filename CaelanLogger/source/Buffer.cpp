#include "Buffer.h"
#include <cstring>
Buffer::Buffer() : capacity_(2000), size_(0), remaining_(capacity_)
{
	buffer = std::make_unique<char[]>(capacity_);
}
Buffer::Buffer(size_t capacity) : capacity_(capacity), size_(0), remaining_(capacity)
{
	buffer = std::make_unique<char[]>(capacity);
}
bool Buffer::add(const char *src, size_t len)
{
	if (len + size_ > capacity_)
	{
		return false;
	}

	std::memcpy(&buffer[size_], src, len);
	size_ += len;
	remaining_ -= len;
	return true;
}

bool Buffer::add(const char src)
{
	if (1 + size_ > capacity_)
	{
		return false;
	}

	buffer[size_++] = src;
	remaining_--;
	return true;
}

void Buffer::reset()
{
	size_ = 0;
	remaining_ = capacity_;
	lineCount_ = 0;
}