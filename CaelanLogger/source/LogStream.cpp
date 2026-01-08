#include "LogStream.h"
#include <algorithm>
#include <cstring> 
#include "Level.h"

LogStream::LogStream(ThreadLogger* target, CaelanLogger::Level level) : target(target)
{
    cur_buffer = target ? target->cur_buffer : nullptr;
    if (!cur_buffer) return;

    if (cur_buffer->getRemaining() < kMaxLineLength)
    {
        target->handoff();
		// after handoff, the target's cur_buffer will change
		// if we get nullptr, we just leave cur_buffer as nullptr and drop further writes
        cur_buffer = target ? target->cur_buffer : nullptr;
    }
    addLevel(level);
    addTime();
}

LogStream::~LogStream()
{
    if (cur_buffer) cur_buffer->add('\n');
}

LogStream& LogStream::operator<<(bool express) {
    if (!cur_buffer) return *this;

    const char* s = express ? "true" : "false";
    const int len = express ? 4 : 5;

    cur_buffer->add(s, len);
    return *this;
}

LogStream& LogStream::operator<<(int number) {
    if (!cur_buffer) return *this;

    convertInt(number);
    return *this;
}

LogStream& LogStream::operator<<(unsigned int number) {
    if (!cur_buffer) return *this;

    convertInt(number);
    return *this;
}

LogStream& LogStream::operator<<(long number) {
    if (!cur_buffer) return *this;

    convertInt(number);
    return *this;
}

LogStream& LogStream::operator<<(unsigned long number) {
    if (!cur_buffer) return *this;

    convertInt(number);
    return *this;
}

LogStream& LogStream::operator<<(long long number) {
    if (!cur_buffer) return *this;

    convertInt(number);
    return *this;
}

LogStream& LogStream::operator<<(unsigned long long number) {
    if (!cur_buffer) return *this;

    convertInt(number);
    return *this;
}

LogStream& LogStream::operator<<(float number) {
    *this << static_cast<double>(number);
    return *this;
}

LogStream& LogStream::operator<<(double number) {
    if (!cur_buffer) return *this;

    char temp[32];
    int len = std::snprintf(temp, sizeof(temp), "%.12g", number);
    if (len <= 0) return *this;

    cur_buffer->add(temp, len);
    return *this;
}

LogStream& LogStream::operator<<(const char str) {
    if (!cur_buffer) return *this;

    cur_buffer->add(str);
    return *this;
}

LogStream& LogStream::operator<<(const char* str) {
    if (!cur_buffer) return *this;

    int len = (int)std::strlen(str);
    cur_buffer->add(str, len);
    return *this;
}

LogStream& LogStream::operator<<(const unsigned char* str) {
    const char* s = reinterpret_cast<const char*>(str);
    if (!cur_buffer) return *this;

    int len = std::strlen(s);
    cur_buffer->add(s, len);
    return *this;
}

LogStream& LogStream::operator<<(const std::string& str) {
    if (!cur_buffer) return *this;

    int len = str.length();
    cur_buffer->add(str.c_str(), len);
    return *this;
}

void LogStream::addLevel(CaelanLogger::Level level) {
    if (!cur_buffer) return;
    
    switch (level)  
    {
    case CaelanLogger::Level::INFO:
        cur_buffer->add("INFO ", 5); 
        break;
    case CaelanLogger::Level::DEBUG:
		cur_buffer->add("DEBUG ", 6);
        break;
    case CaelanLogger::Level::WARNING:
        cur_buffer->add("WARNING ", 8);
        break;
    case CaelanLogger::Level::ERROR:
        cur_buffer->add("ERROR ", 7);
        break;
    case CaelanLogger::Level::FATAL:
        cur_buffer->add("FATAL ", 6);
        break;
    default:
		cur_buffer->add("INFO ", 5);
        break;
    }
}

void LogStream::addTime()
{
    if (!cur_buffer) return;
    std::string logTime = LogTime::nowString();
    cur_buffer->add(logTime.c_str(), logTime.length());
    cur_buffer->add(" ", 1);
}
