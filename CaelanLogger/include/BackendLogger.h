#pragma once
#include <atomic>
#include <ThreadLogger.h>
#include <memory>
#include <thread>
#include <vector>
#include "FileUtil.h"
#include "Buffer.h"

static constexpr int QUEUE_SIZE = 32;

class BackendLogger
{
public:
	std::atomic<bool> freeAvailable_{true};
	std::atomic<bool> running_{false};
	std::thread writer_;
	BackendLogger(size_t, std::string dir = "./log");
	~BackendLogger();
	void start();
	void run();
	void stop();
	void submitAndAcquire(Buffer *&);
	void write();
	void restart(size_t);
	Buffer *get_free_buffer();
	void record_drop() { futil_->add_dropped(); }

private:
	std::atomic_flag spinlock_ = ATOMIC_FLAG_INIT;
	Buffer *pendingQue_[QUEUE_SIZE];
	size_t pendingQueHead_{0};
	size_t pendingQueTail_{0};
	std::atomic<size_t> pendingQueSize_{0};
	Buffer *freeQue_[QUEUE_SIZE];
	size_t freeQueHead_{0};
	size_t freeQueTail_{0};
	size_t freeQueSize_{0};
	std::unique_ptr<FileUtil> futil_;
};
