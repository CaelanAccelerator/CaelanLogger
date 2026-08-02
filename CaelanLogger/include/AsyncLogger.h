#pragma once
#include <atomic>
#include <memory>
#include <unordered_map>
#include "ThreadLogger.h"
#include "LogStream.h"
#include "Level.h"

template <typename BackendT>
class AsyncLogger
{
public:
    explicit AsyncLogger(size_t bufSize, size_t queueSize = 32, std::string logDir = "")
        : backend_(bufSize, queueSize, std::move(logDir)), bufSize_(bufSize), queueSize_(queueSize)
    {
    }

    ~AsyncLogger()
    {
        shutdown();
    }

    // Non-copyable, non-movable (owns a running thread)
    AsyncLogger(const AsyncLogger &) = delete;
    AsyncLogger &operator=(const AsyncLogger &) = delete;

    ThreadLogger<BackendT> &tls()
    {
        auto [it, _] = tlsMap().try_emplace(&backend_, bufSize_, &backend_);
        return it->second;
    }

    // Flushes and joins the backend's writer thread. Safe to call more than
    // once (including implicitly via the destructor).
    void shutdown()
    {
        tlsMap().erase(&backend_);
        backend_.stop();
    }

    void flush()
    {
        tls().handoff(true);
    }

private:
    static std::unordered_map<BackendT *, ThreadLogger<BackendT>> &tlsMap()
    {
        thread_local std::unordered_map<BackendT *, ThreadLogger<BackendT>> map;
        return map;
    }

    BackendT backend_;
    size_t bufSize_;
    size_t queueSize_;
};

#define LOG_TO(logger, LEVEL) LogStream(&(logger).tls(), CaelanLogger::LEVEL)
#define LOG_INFO_TO(logger) LOG_TO(logger, INFO)
#define LOG_WARN_TO(logger) LOG_TO(logger, WARNING)
#define LOG_ERROR_TO(logger) LOG_TO(logger, ERROR)
#define LOG_DEBUG_TO(logger) LOG_TO(logger, DEBUG)
