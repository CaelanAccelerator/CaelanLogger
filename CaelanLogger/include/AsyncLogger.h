#pragma once
#include <atomic>
#include "BackendLogger.h"
#include "ThreadLogger.h"
#include "LogStream.h"
#include "Level.h"

class AsyncLogger
{
public:
    static void init(size_t bufSize, std::string logDir = "") noexcept
    {
        s_bufSize_.store(bufSize, std::memory_order_relaxed);
        logDir_ = std::move(logDir);
    }

    static AsyncLogger &getInstance()
    {
        static AsyncLogger instance(s_bufSize_.load(std::memory_order_relaxed));
        return instance;
    }

    ThreadLogger &tls()
    {
        thread_local ThreadLogger tl(bufSize_, &backend_);
        return tl;
    }

    void shutdown()
    {
        backend_.stop();
    }

    // This function will restart the backend logger with a new buffer size, but may cause log loss
    void restart(size_t bufSize)
    {
        init(bufSize);
        backend_.stop();
        backend_.restart(bufSize);
        backend_.start();
    }

private:
    explicit AsyncLogger(size_t bufSize)
        : backend_(bufSize, logDir_), bufSize_(bufSize)
    {
        backend_.start();
    }

    ~AsyncLogger()
    {
        backend_.stop();
    }

private:
    static inline std::string logDir_{""};
    static inline std::atomic<size_t> s_bufSize_{2028};
    BackendLogger backend_;
    const size_t bufSize_;
};

#define LOG(LEVEL) LogStream(&AsyncLogger::getInstance().tls(), CaelanLogger::LEVEL)
#define LOG_INFO() LOG(INFO)
#define LOG_WARN() LOG(WARNING)
#define LOG_ERROR() LOG(ERROR)
#define LOG_DEBUG() LOG(DEBUG)