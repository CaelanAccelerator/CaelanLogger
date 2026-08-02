#pragma once
#include <cstddef> // Add this line to ensure std::size_t is declared
#include <memory>
using std::size_t;

// property of a buffer
constexpr size_t maxSize{2000};

class Buffer
{
public:
	Buffer();
	Buffer(size_t);
	Buffer(const Buffer &) = delete;						// to prevent double free after copy construction
	Buffer &operator=(const Buffer &) = delete; // to prevent double free after copy construction
	bool add(const char *, size_t);
	bool add(const char);
	size_t size() const { return size_; }
	void increaseSize(size_t inc) { size_ += inc; remaining_ -= inc; }
	size_t capacity() const { return capacity_; }
	char *getBuffer() const { return buffer.get(); }
	size_t remaining() const { return remaining_; }
	size_t lineCount() const { return lineCount_; }
	void incrementLineCount() { lineCount_++; }
	void reset();
	size_t idx() const { return idx_; }
	void setIdx(size_t idx) { idx_ = idx; }

private:
	std::unique_ptr<char[]> buffer;
	size_t size_;
	size_t capacity_;
	size_t remaining_;
	size_t lineCount_{0};
	size_t idx_;
};
