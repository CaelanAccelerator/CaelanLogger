#include "ThreadLogger.h"
#include "BackendLogger.h"
#include <cstring>
#include <cstdio>

ThreadLogger::ThreadLogger(size_t sizeBuf, BackendLogger *bl)
    : backendLogger_(bl), curBuffer_(bl->get_free_buffer())
{
}

ThreadLogger::~ThreadLogger()
{
    if (curBuffer_ && backendLogger_)
        backendLogger_->submitAndAcquire(std::move(curBuffer_));
}

void ThreadLogger::handoff(bool force)
{
    if (!backendLogger_)
        return;
    if (!backendLogger_->freeAvailable_.load(std::memory_order_acquire) && !force)
        return;
    if (!curBuffer_)
    {
        curBuffer_ = backendLogger_->get_free_buffer();
        return;
    }

    curBuffer_ = backendLogger_->submitAndAcquire(std::move(curBuffer_));
}