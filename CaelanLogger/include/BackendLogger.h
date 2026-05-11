#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <ThreadLogger.h>
#include <memory>
#include <thread>
#include <vector>
#include "FileUtil.h"
#include "Buffer.h"

class BackendLogger
{
public:
	std::atomic<bool> freeAvailable_{true};
	std::atomic<bool> running_{false};
	std::thread writer_;
	BackendLogger(size_t bufSize, size_t queueSize, std::string dir = "./log");
	~BackendLogger();
	void start();
	void run();
	void stop();
	void submitAndAcquire(Buffer *&);
	void write();
	Buffer *get_free_buffer();
	void record_drop();

private:
	friend class AsyncLogger;
	std::mutex cvMutex_;
	std::condition_variable cv_;
	std::atomic_flag spinlockPen_ = ATOMIC_FLAG_INIT;
	std::atomic_flag spinlockFree_ = ATOMIC_FLAG_INIT;
	size_t queueSize_;
	std::unique_ptr<Buffer *[]> pendingQue_;
	size_t pendingQueHead_{0};
	size_t pendingQueTail_{0};
	std::atomic<size_t> pendingQueSize_{0};
	std::unique_ptr<Buffer *[]> freeQue_;
	size_t freeQueHead_{0};
	size_t freeQueTail_{0};
	size_t freeQueSize_{0};
	std::unique_ptr<FileUtil> futil_;
};
