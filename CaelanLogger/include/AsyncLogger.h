#pragma once
#include <atomic>
#include <unordered_map>
#include "BackendLogger.h"
#include "ThreadLogger.h"
#include "LogStream.h"
#include "Level.h"

class AsyncLogger
{
public:
    explicit AsyncLogger(size_t bufSize, size_t queueSize = 32, std::string logDir = "")
        : backend_(bufSize, queueSize, std::move(logDir))
        , bufSize_(bufSize)
        , queueSize_(queueSize)
    {
        backend_.start();
    }

    ~AsyncLogger()
    {
        backend_.stop();
    }

    // Non-copyable, non-movable (owns a running thread)
    AsyncLogger(const AsyncLogger &) = delete;
    AsyncLogger &operator=(const AsyncLogger &) = delete;

    ThreadLogger &tls()
    {
        thread_local std::unordered_map<BackendLogger *, ThreadLogger> map;
        auto [it, _] = map.try_emplace(&backend_, bufSize_, &backend_);
        return it->second;
    }

    void shutdown()
    {
        backend_.stop();
    }

    void flush()
    {
        tls().handoff(true);
    }

private:
    BackendLogger backend_;
    size_t bufSize_;
    size_t queueSize_;
};

#define LOG_TO(logger, LEVEL)  LogStream(&(logger).tls(), CaelanLogger::LEVEL)
#define LOG_INFO_TO(logger)    LOG_TO(logger, INFO)
#define LOG_WARN_TO(logger)    LOG_TO(logger, WARNING)
#define LOG_ERROR_TO(logger)   LOG_TO(logger, ERROR)
#define LOG_DEBUG_TO(logger)   LOG_TO(logger, DEBUG)
