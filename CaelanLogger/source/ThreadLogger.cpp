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
        backendLogger_->submitOnly(curBuffer_);
    curBuffer_    = nullptr;
    backendLogger_ = nullptr;
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

    backendLogger_->submitAndAcquire(curBuffer_);
}