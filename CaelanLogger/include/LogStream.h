#pragma once
#include <string>
#include "Buffer.h"
#include "ThreadLogger.h"
#include "Level.h"
#include "TimeUtil.h"

template <typename BackendT>
class LogStream
{
public:
    LogStream() = default;
    LogStream(ThreadLogger<BackendT> *, CaelanLogger::Level);
    ~LogStream();
    // convert different types of data to chars and load in current buffer
    LogStream &operator<<(bool express);
    LogStream &operator<<(int);
    LogStream &operator<<(unsigned int);
    LogStream &operator<<(long);
    LogStream &operator<<(unsigned long);
    LogStream &operator<<(long long);
    LogStream &operator<<(unsigned long long);
    LogStream &operator<<(float number);
    LogStream &operator<<(double);
    LogStream &operator<<(char str);
    LogStream &operator<<(const char *);
    LogStream &operator<<(const unsigned char *);
    LogStream &operator<<(const std::string &);

    size_t getMaxLineLength() const { return kMaxLineLength; }

private:
    static const size_t kMaxLineLength = 1028;
    ThreadLogger<BackendT> *target_;
    Buffer *curBuffer_;

    void addLevel(CaelanLogger::Level);
    void addTime();

    template <typename T>
    void convertInt(T number);
};

template <typename BackendT>
template <typename T>
void LogStream<BackendT>::convertInt(T number)
{
    if (curBuffer_->remaining() < 32)
    {
        return;
    }

    if (!number)
    {
        curBuffer_->add('0');
        return;
    }
    char *ptr = curBuffer_->getBuffer() + curBuffer_->size();

    if (number < 0)
    {
        *ptr++ = '-';
        curBuffer_->increaseSize(1);
    }
    size_t i = 0;
    static const char digits[] = "9876543210123456789";
    static const char *zero = digits + 9;
    for (; number != 0; i++)
    {
        int remainder = static_cast<int>(number % 10);
        ptr[i] = zero[remainder];
        number /= 10;
    }
    std::reverse(ptr, ptr + i);
    curBuffer_->increaseSize(i);
    return;
}

template <typename BackendT>
LogStream<BackendT>::LogStream(ThreadLogger<BackendT> *target, CaelanLogger::Level level) : target_(target)
{
    if (!target_)
        return;

    curBuffer_ = target_->getCurBuffer();

    if (!curBuffer_)
    {
        target_->handoff();
        curBuffer_ = target_->getCurBuffer();
    }
    if (!curBuffer_)
        return;

    if (curBuffer_->remaining() < kMaxLineLength)
    {
        target_->handoff();
        curBuffer_ = target_->getCurBuffer();
        if (!curBuffer_ || curBuffer_->remaining() < kMaxLineLength) // to
        {
            target_->recordDrop();
            curBuffer_ = nullptr;
            target_ = nullptr; // prevent destructor from double-counting
            return;
        }
    }
    addLevel(level);
    addTime();
}

template <typename BackendT>
LogStream<BackendT>::~LogStream()
{
    if (curBuffer_)
    {
        curBuffer_->add('\n');
        curBuffer_->incrementLineCount();
    }
    else if (target_)
        target_->recordDrop();
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(bool express)
{
    if (!curBuffer_)
        return *this;

    const char *s = express ? "true" : "false";
    const size_t len = express ? 4 : 5;

    curBuffer_->add(s, len);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(int number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(unsigned int number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(long number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(unsigned long number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(long long number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(unsigned long long number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(float number)
{
    *this << static_cast<double>(number);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(double number)
{
    if (!curBuffer_)
        return *this;

    char temp[32];
    int len = std::snprintf(temp, sizeof(temp), "%.12g", number);
    if (len <= 0)
        return *this;

    curBuffer_->add(temp, len);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(const char str)
{
    if (!curBuffer_)
        return *this;

    curBuffer_->add(str);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(const char *str)
{
    if (!curBuffer_)
        return *this;

    size_t len = std::strlen(str);
    curBuffer_->add(str, len);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(const unsigned char *str)
{
    const char *s = reinterpret_cast<const char *>(str);
    if (!curBuffer_)
        return *this;

    size_t len = std::strlen(s);
    curBuffer_->add(s, len);
    return *this;
}

template <typename BackendT>
LogStream<BackendT> &LogStream<BackendT>::operator<<(const std::string &str)
{
    if (!curBuffer_)
        return *this;

    size_t len = str.length();
    curBuffer_->add(str.c_str(), len);
    return *this;
}

template <typename BackendT>
void LogStream<BackendT>::addLevel(CaelanLogger::Level level)
{
    if (!curBuffer_)
        return;

    switch (level)
    {
    case CaelanLogger::Level::INFO:
        curBuffer_->add("INFO ", 5);
        break;
    case CaelanLogger::Level::DEBUG:
        curBuffer_->add("DEBUG ", 6);
        break;
    case CaelanLogger::Level::WARNING:
        curBuffer_->add("WARNING ", 8);
        break;
    case CaelanLogger::Level::ERROR:
        curBuffer_->add("ERROR ", 7);
        break;
    case CaelanLogger::Level::FATAL:
        curBuffer_->add("FATAL ", 6);
        break;
    default:
        curBuffer_->add("INFO ", 5);
        break;
    }
}

template <typename BackendT>
void LogStream<BackendT>::addTime()
{
    if (!curBuffer_)
        return;
    std::string logTime = LogTime::nowString();
    curBuffer_->add(logTime.c_str(), logTime.length());
    curBuffer_->add(" ", 1);
}
