#pragma once
#include <ThreadLogger.h>
#include <string>
#include <cstring>
#include <Buffer.h>
#include <algorithm>

class BackendLogger;

class ThreadLogger
{
public:
	ThreadLogger(size_t, BackendLogger*);
	~ThreadLogger();
	void handoff();
    ThreadLogger& operator<<(bool v);
    ThreadLogger& operator<<(int v);
    ThreadLogger& operator<<(unsigned int v);
    ThreadLogger& operator<<(long v);
    ThreadLogger& operator<<(unsigned long v);
    ThreadLogger& operator<<(long long v);
    ThreadLogger& operator<<(unsigned long long v);

    ThreadLogger& operator<<(float v);
    ThreadLogger& operator<<(double v);

    ThreadLogger& operator<<(char v);
    ThreadLogger& operator<<(const char* v);
    ThreadLogger& operator<<(const unsigned char* v);
    ThreadLogger& operator<<(const std::string& v);

private:
	Buffer* cur_buffer;
	BackendLogger* backend_logger;
    template<typename T>
    bool convertInt(T number) {
        if (cur_buffer->capacity - cur_buffer->size < 32)
        {
            return false;
        }

        if (!number) {
            cur_buffer->buffer[cur_buffer->size++] = '0';
            return true;
        }
        char* ptr = cur_buffer->buffer + cur_buffer->size;

        if (number < 0)
        {
            *ptr++ = '-';
            cur_buffer->size += 1;
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
        cur_buffer->size += i;
        return true;
    }
};
