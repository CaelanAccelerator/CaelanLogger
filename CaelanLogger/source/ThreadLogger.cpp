#include "ThreadLogger.h"
#include "BackendLogger.h"
#include <cstring>
#include <cstdio>

ThreadLogger::ThreadLogger(size_t sizeBuf, BackendLogger* bl)
    : backend_logger(bl), cur_buffer(new Buffer(sizeBuf))
{
}

ThreadLogger::~ThreadLogger()
{
    delete cur_buffer;
    cur_buffer = nullptr;
    backend_logger = nullptr;
}

void ThreadLogger::handoff()
{
    if (!backend_logger) return;
    if (!backend_logger->freeAvailable.load(std::memory_order_acquire))
        return;

    backend_logger->submit_and_acquire(cur_buffer); 
}

ThreadLogger& ThreadLogger::operator<<(bool express) {
    if (!cur_buffer) return *this;

    const char* s = express ? "true" : "false";
    const int len = express ? 4 : 5;

    bool result = cur_buffer->add(s, len);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        cur_buffer->add(s, len);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(int number) {
    if (!cur_buffer) return *this;

    bool result = convertInt(number);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        convertInt(number);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(unsigned int number) {
    if (!cur_buffer) return *this;

    bool result = convertInt(number);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        convertInt(number);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(long number) {
    if (!cur_buffer) return *this;

    bool result = convertInt(number);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        convertInt(number);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(unsigned long number) {
    if (!cur_buffer) return *this;

    bool result = convertInt(number);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        convertInt(number);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(long long number) {
    if (!cur_buffer) return *this;

    bool result = convertInt(number);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        convertInt(number);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(unsigned long long number) {
    if (!cur_buffer) return *this;

    bool result = convertInt(number);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        convertInt(number);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(float number) {
    *this << static_cast<double>(number);
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(double number) {
    if (!cur_buffer) return *this;

    char temp[32];
    int len = std::snprintf(temp, sizeof(temp), "%.12g", number);
    if (len <= 0) return *this;

    bool result = cur_buffer->add(temp, len);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        cur_buffer->add(temp, len);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(const char str) {
    if (!cur_buffer) return *this;

    bool result = cur_buffer->add(str);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        cur_buffer->add(str);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(const char* str) {
    if (!cur_buffer) return *this;

    int len = (int)std::strlen(str);
    bool result = cur_buffer->add(str, len);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        cur_buffer->add(str, len);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(const unsigned char* str) {
    const char* s = reinterpret_cast<const char*>(str);
    if (!cur_buffer) return *this;

    int len = std::strlen(s);
    bool result = cur_buffer->add(s, len);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        cur_buffer->add(s, len);
    }
    return *this;
}

ThreadLogger& ThreadLogger::operator<<(const std::string& str) {
    if (!cur_buffer) return *this;

    int len = str.length();
    bool result = cur_buffer->add(str.c_str(), len);
    if (!result)
    {
        handoff();
        if (!cur_buffer) return *this;
        cur_buffer->add(str.c_str(), len);
    }
    return *this;
}
