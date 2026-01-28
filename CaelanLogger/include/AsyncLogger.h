#pragma once
#include <atomic>
#include "BackendLogger.h"

class AsyncLogger {
public:
    static void init(size_t bufSize) noexcept {
        s_bufSize.store(bufSize, std::memory_order_relaxed);
    }

    static AsyncLogger& getInstance() {
        static AsyncLogger instance(s_bufSize.load(std::memory_order_relaxed));
        return instance;
    }

    ThreadLogger& tls() {
        thread_local ThreadLogger tl(bufSize_, &backend_);
        return tl;
    }

    void shutdown()
    {
        backend_.stop(); 
    }

	//This function will restart the backend logger with a new buffer size, but may cause log loss
    void restart(size_t bufSize)
    {
		init(bufSize);
        backend_.stop();
        backend_.restart(bufSize);
		backend_.start();
	}

private:
    explicit AsyncLogger(size_t bufSize)
        : backend_(bufSize), bufSize_(bufSize) {
        backend_.start();
    }

    ~AsyncLogger() 
    {
        backend_.stop();
    }

private:
    BackendLogger backend_;
    const size_t bufSize_;                      
    static inline std::atomic<size_t> s_bufSize{ 2028 };
};
