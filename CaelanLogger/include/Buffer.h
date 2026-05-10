#pragma once
#include <cstddef> // Add this line to ensure std::size_t is declared
using std::size_t;

//property of a buffer
constexpr size_t maxSize{ 2000 };

class Buffer
{
public:
	Buffer();
	Buffer(size_t);
	Buffer(const Buffer&) = delete;//to prevent double free after copy construction
	Buffer& operator=(const Buffer&) = delete;//to prevent double free after copy construction
	~Buffer();
	bool add(const char*, size_t);
	bool add(const char);
	size_t getSize() { return size; }
	size_t getCapacity() { return capacity; }
	char* getBuffer() { return buffer; }	
	size_t getRemaining() { return remaining; }
	size_t getLineCount() { return line_count; }
	void reset();
	friend class LogStream;
private:
	char* buffer;
	size_t size;
	size_t capacity;
	size_t remaining;
	size_t line_count{0};
};

