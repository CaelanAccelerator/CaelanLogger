//#include "ThreadLogger.h"
//#include <algorithm>
//#include <cstring> 
//
//ThreadLogger::ThreadLogger(Buffer* buf):cur_buffer(buf){}
//
//
//void ThreadLogger::assignBuffer(Buffer* buffer) {
//    cur_buffer = buffer;
//}
//
//template<typename T>
//void ThreadLogger::convertInt(T number) {
//    if (cur_buffer->capacity - cur_buffer->size < 32)
//        return;
//
//    if (!number) {
//        cur_buffer->buffer[cur_buffer->size++] = '0';
//        return;
//    }
//    char* ptr = cur_buffer->buffer + cur_buffer->size;
//
//    if (number < 0)
//    {
//        *ptr++ = '-';
//        cur_buffer->size += 1;
//    }
//    size_t i = 0;
//    static const char digits[] = "9876543210123456789";
//    static const char* zero = digits + 9;
//    for (; number != 0; i++)
//    {
//        int remainder = static_cast<int>(number % 10);
//        ptr[i] = zero[remainder];
//        number /= 10;
//    }
//    std::reverse(ptr, ptr + i);
//    cur_buffer->size += i;
//}
//
//ThreadLogger& ThreadLogger::operator<<(bool express) {
//    express ? cur_buffer->add("true", 4) : cur_buffer->add("false", 5);
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(int number) {
//    convertInt(number);
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(unsigned int number) {
//    convertInt(number);
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(long number) {
//    convertInt(number);
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(unsigned long number) {
//    convertInt(number);
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(long long number) {
//    convertInt(number);
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(unsigned long long number) {
//    convertInt(number);
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(float number) {
//    *this << static_cast<double>(number);
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(double number) {
//    char temp[32];
//    snprintf(temp, sizeof(temp), "%.12g", number);
//    cur_buffer->add(temp, std::strlen(temp));
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(const char str) {
//    cur_buffer->add(str);
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(const char* str) {
//    cur_buffer->add(str, std::strlen(str));
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(const unsigned char* str) {
//    cur_buffer->add(reinterpret_cast<const char*>(str), std::strlen(reinterpret_cast<const char*>(str)));
//    return *this;
//}
//
//ThreadLogger& ThreadLogger::operator<<(const std::string& str) {
//    cur_buffer->add(str.c_str(), str.length());
//    return *this;
//}