#pragma once
#include <atomic>
#include <ThreadLogger.h>
#include <memory>
#include <thread>
#include <vector>
#include "FileUtil.h"
#include "Buffer.h"

#ifdef CAELOGGER_TESTING
static constexpr int QUEUE_SIZE = 10000;
#else
static constexpr int QUEUE_SIZE = 32;
#endif
class BackendLogger
{
public:
	std::atomic<bool> freeAvailable{true};
	std::atomic<bool> running{false};
	std::thread writer;
	BackendLogger(size_t, std::string dir = "./log");
	~BackendLogger();
	void start();
	void run();
	void stop();
	void submit_and_acquire(Buffer *&);
	void write();
	void restart(size_t);
	Buffer *get_free_buffer();

private:
	std::atomic_flag spinlock = ATOMIC_FLAG_INIT;
	Buffer *pendingQue[QUEUE_SIZE];
	std::vector<Buffer *> pendingBuffers;
	size_t pendingQueFront{0};
	size_t pendingQueBack{0};
	std::atomic<size_t> pendingQueSize{0};
	Buffer *freeQue[QUEUE_SIZE];
	size_t freeQueFront{0};
	size_t freeQueBack{0};
	size_t freeQueSize{0};
	std::unique_ptr<FileUtil> futil;
};
