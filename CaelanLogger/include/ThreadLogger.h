#pragma once
#include <string>
#include <cstring>
#include <Buffer.h>
#include <algorithm>

class BackendLogger;

class ThreadLogger
{
public:
	ThreadLogger(size_t, BackendLogger *);
	~ThreadLogger();
	void handoff(bool force = false);

private:
	unsigned long long lostLogs{0};
	Buffer *curBuffer_;
	BackendLogger *backendLogger_;
	friend class LogStream;
	friend class AsyncLogger;
};
