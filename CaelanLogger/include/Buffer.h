#pragma once
#include <cstddef> // Add this line to ensure std::size_t is declared
using std::size_t;

//property of a buffer
constexpr int maxSize{ 2000 };

class Buffer
{
public:
	Buffer();
	Buffer(int);
	~Buffer();
	bool add(const char*, int);
	bool add(const char);
	size_t getSize() { return size; }
	size_t getCapacity() { return capacity; }
	char* getBuffer() { return buffer; }	
	size_t getRemaining() { return remaining; }
	void reset();
	friend class LogStream;
private:
	char* buffer;
	int size;
	int capacity;
	int remaining;
};

