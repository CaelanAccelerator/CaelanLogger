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
        : backend_(std::make_unique<BackendT>(bufSize, queueSize, std::move(logDir))),
          bufSize_(bufSize), queueSize_(queueSize)
    {
    }

    ~AsyncLogger()
    {
        shutdownAll();
    }

    // Non-copyable, non-movable (owns a running thread)
    AsyncLogger(const AsyncLogger &) = delete;
    AsyncLogger &operator=(const AsyncLogger &) = delete;

    // Returns the calling thread's ThreadLogger, registering one on first
    // use. Returns nullptr once shutdownAll() has run, instead of silently
    // registering a fresh ThreadLogger against an already-stopped backend.
    ThreadLogger<BackendT> *tls()
    {
        if (!alive_.load(std::memory_order_acquire))
            return nullptr;

        auto [it, _] = tlsMap().try_emplace(backend_.get(), bufSize_, backend_.get());
        return &it->second;
    }

    // Releases the calling thread's ThreadLogger, handing its pending buffer
    // to the backend via ~ThreadLogger(). Safe to call from any thread at
    // any time; does not affect other threads or the backend itself.
    void shutdownTL()
    {
        tlsMap().erase(backend_.get());
    }

    // Stops accepting new logs, then destroys the backend - its destructor
    // flushes and joins the writer thread. Every other thread using this
    // logger must have already called shutdownTL() (or be done logging)
    // before this runs - it does not wait for them. Safe to call more than
    // once, including implicitly via the destructor.
    void shutdownAll()
    {
        alive_.store(false, std::memory_order_release);
        shutdownTL();
        backend_.reset();
    }

    void flush()
    {
        if (auto *t = tls())
            t->handoff();
    }

private:
    static std::unordered_map<BackendT *, ThreadLogger<BackendT>> &tlsMap()
    {
        thread_local std::unordered_map<BackendT *, ThreadLogger<BackendT>> map;
        return map;
    }

    std::unique_ptr<BackendT> backend_;
    size_t bufSize_;
    size_t queueSize_;
    std::atomic<bool> alive_{true};
};

#define LOG_TO(logger, LEVEL) LogStream((logger).tls(), CaelanLogger::LEVEL)
#define LOG_INFO_TO(logger) LOG_TO(logger, INFO)
#define LOG_WARN_TO(logger) LOG_TO(logger, WARNING)
#define LOG_ERROR_TO(logger) LOG_TO(logger, ERROR)
#define LOG_DEBUG_TO(logger) LOG_TO(logger, DEBUG)
