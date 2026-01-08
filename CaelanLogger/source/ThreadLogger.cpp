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
