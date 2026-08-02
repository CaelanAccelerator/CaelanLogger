#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <ThreadLogger.h>
#include <memory>
#include <thread>
#include <vector>
#include "NormalWriter.h"
#include "Buffer.h"
#include "MPSCSpinLockQueue.h"
#include "SPMCSpinLockQueue.h"

class SharedBackend
{
public:
	std::atomic<bool> running_{false};
	SharedBackend(size_t bufSize, size_t queueSize, std::string dir = "./log");
	~SharedBackend();

	std::unique_ptr<Buffer> acquire();
	void submit(std::unique_ptr<Buffer>);
	void record_drop();
	void stop();

private:
	friend class BackendLoggerTestAccess;
	std::thread writer_;
	std::mutex cvMutex_;
	std::condition_variable cv_;
	std::unique_ptr<std::unique_ptr<Buffer>[]> bufferPool_;
	size_t poolCapacity_{0};
	std::unique_ptr<MPSCSpinLockQueue<size_t>> submittedIdxes_;
	std::unique_ptr<SPMCSpinLockQueue<size_t>> freeIdxes_;
	std::unique_ptr<NormalWriter> futil_;

	void write();
	void start();
	void run();
};
