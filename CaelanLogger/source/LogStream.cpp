#include "LogStream.h"
#include "BackendLogger.h"
#include <algorithm>
#include <cstring>
#include "Level.h"

LogStream::LogStream(ThreadLogger *target, CaelanLogger::Level level) : target_(target)
{
    curBuffer_ = target_ ? target_->curBuffer_ : nullptr;

    if (!curBuffer_)
    {
        target_->handoff();
        curBuffer_ = target_ ? target_->curBuffer_ : nullptr;
    }
    if (!curBuffer_)
        return;

    if (curBuffer_->getRemaining() < kMaxLineLength)
    {
        target_->handoff();
        curBuffer_ = target_ ? target_->curBuffer_ : nullptr;
        if (!curBuffer_ || curBuffer_->getRemaining() < kMaxLineLength) // to
        {
            target_->backendLogger_->record_drop();
            curBuffer_ = nullptr;
            target_ = nullptr; // prevent destructor from double-counting
            return;
        }
    }
    addLevel(level);
    addTime();
}

LogStream::~LogStream()
{
    if (curBuffer_)
    {
        curBuffer_->add('\n');
        curBuffer_->line_count++;
    }
    else if (target_)
        target_->backendLogger_->record_drop();
}

LogStream &LogStream::operator<<(bool express)
{
    if (!curBuffer_)
        return *this;

    const char *s = express ? "true" : "false";
    const size_t len = express ? 4 : 5;

    curBuffer_->add(s, len);
    return *this;
}

LogStream &LogStream::operator<<(int number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

LogStream &LogStream::operator<<(unsigned int number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

LogStream &LogStream::operator<<(long number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

LogStream &LogStream::operator<<(unsigned long number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

LogStream &LogStream::operator<<(long long number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

LogStream &LogStream::operator<<(unsigned long long number)
{
    if (!curBuffer_)
        return *this;

    convertInt(number);
    return *this;
}

LogStream &LogStream::operator<<(float number)
{
    *this << static_cast<double>(number);
    return *this;
}

LogStream &LogStream::operator<<(double number)
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

LogStream &LogStream::operator<<(const char str)
{
    if (!curBuffer_)
        return *this;

    curBuffer_->add(str);
    return *this;
}

LogStream &LogStream::operator<<(const char *str)
{
    if (!curBuffer_)
        return *this;

    size_t len = std::strlen(str);
    curBuffer_->add(str, len);
    return *this;
}

LogStream &LogStream::operator<<(const unsigned char *str)
{
    const char *s = reinterpret_cast<const char *>(str);
    if (!curBuffer_)
        return *this;

    size_t len = std::strlen(s);
    curBuffer_->add(s, len);
    return *this;
}

LogStream &LogStream::operator<<(const std::string &str)
{
    if (!curBuffer_)
        return *this;

    size_t len = str.length();
    curBuffer_->add(str.c_str(), len);
    return *this;
}

void LogStream::addLevel(CaelanLogger::Level level)
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

void LogStream::addTime()
{
    if (!curBuffer_)
        return;
    std::string logTime = LogTime::nowString();
    curBuffer_->add(logTime.c_str(), logTime.length());
    curBuffer_->add(" ", 1);
}
