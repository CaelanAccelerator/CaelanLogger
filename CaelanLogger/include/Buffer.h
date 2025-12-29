#pragma once

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
	int getSize();
	int getCapacity();
	char* getBuffer();
	void reset();
	friend class ThreadLogger;
	friend class ThreadLogger;
private:
	char* buffer;
	int size;
	int capacity;
};

