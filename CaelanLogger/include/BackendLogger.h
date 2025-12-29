#pragma once
#include <atomic>
#include <ThreadLogger.h>
#include <memory>
#include <thread>
#include "FileUtil.h"

static constexpr int QUEUE_SIZE = 38;

class BackendLogger
{
public:
	std::atomic<bool> freeAvailable{ true };
	std::atomic<bool> running{ false };
	std::thread writer;
	BackendLogger(size_t, std::string dir = "./log");
	~BackendLogger();
	void start();
	void run();
	void stop();
	void submit_and_acquire(Buffer*&);
	void store();
private:
	std::atomic_flag spinlock = ATOMIC_FLAG_INIT;
	Buffer* pendingQue[QUEUE_SIZE];
	int pendingQueFront{ 0 };
	int pendingQueBack{ 0 };
	int pendingQueSize{ 0 };
	Buffer* freeQue[QUEUE_SIZE];
	int freeQueFront{ 0 };
	int freeQueBack{ 0 };
	int freeQueSize{ 0 };
	std::unique_ptr<FileUtil> futil;

	void roll();
	bool shouldRoll(size_t);
	std::string generateFileName();
};
