#pragma once
#include <string>
#include <memory>
#include "Buffer.h" 
#include "ThreadLogger.h"
#include "Level.h"
#include "TimeUtil.h"

class LogStream
{
public:
	LogStream() = default;
    LogStream(ThreadLogger*, CaelanLogger::Level);
	~LogStream();
    //convert different types of data to chars and load in current buffer
    LogStream& operator<<(bool express);
    LogStream& operator<<(int);
    LogStream& operator<<(unsigned int);
    LogStream& operator<<(long);
    LogStream& operator<<(unsigned long);
    LogStream& operator<<(long long);
    LogStream& operator<<(unsigned long long);
    LogStream& operator<<(float number);
    LogStream& operator<<(double);
    LogStream& operator<<(char str);
    LogStream& operator<<(const char*);
    LogStream& operator<<(const unsigned char*);
    LogStream& operator<<(const std::string&);

	size_t getMaxLineLength() const { return kMaxLineLength; }
	friend class ThreadLogger;
private:
	static const size_t kMaxLineLength = 1028;
    ThreadLogger* target_;
    Buffer* curBuffer_;

    void addLevel(CaelanLogger::Level);
	void addTime();

    template<typename T>
    void convertInt(T number);
};

template<typename T>
void LogStream::convertInt(T number) {
    if (curBuffer_->getRemaining() < 32)
    {
        return;
    }

    if (!number) {
        curBuffer_->buffer[curBuffer_->size++] = '0';
        return;
    }
    char* ptr = curBuffer_->buffer + curBuffer_->size;

    if (number < 0)
    {
        *ptr++ = '-';
        curBuffer_->size += 1;
    }
    size_t i = 0;
    static const char digits[] = "9876543210123456789";
    static const char* zero = digits + 9;
    for (; number != 0; i++)
    {
        int remainder = static_cast<int>(number % 10);
        ptr[i] = zero[remainder];
        number /= 10;
    }
    std::reverse(ptr, ptr + i);
    curBuffer_->size += i;
    curBuffer_->remaining = curBuffer_->capacity - curBuffer_->size;
    return;
}



