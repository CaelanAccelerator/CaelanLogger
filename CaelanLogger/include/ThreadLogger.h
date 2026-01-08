#pragma once
#include <ThreadLogger.h>
#include <string>
#include <cstring>
#include <Buffer.h>
#include <algorithm>

class BackendLogger;

class ThreadLogger
{
public:
	ThreadLogger(size_t, BackendLogger*);
	~ThreadLogger();
	void handoff();
	friend class LogStream;
private:
	Buffer* cur_buffer;
	BackendLogger* backend_logger;
};
