#pragma once
#include <string>
#include <cstring>
#include <memory>
#include <Buffer.h>
#include <algorithm>

template <typename BackendT>
class ThreadLogger
{
public:
	ThreadLogger(size_t, BackendT *);
	~ThreadLogger();
	void handoff(bool force = false);
	Buffer *getCurBuffer() const { return curBuffer_.get(); }
	void recordDrop() const
	{
		if (backendLogger_)
		{
			backendLogger_->record_drop();
		}
	}
	unsigned long long getLostLogs() const { return lostLogs; }
	void setLostLogs(unsigned long long n) { lostLogs = n; }

private:
	unsigned long long lostLogs{0};
	std::unique_ptr<Buffer> curBuffer_;
	BackendT *backendLogger_;
};

template <typename BackendT>
ThreadLogger<BackendT>::ThreadLogger(size_t sizeBuf, BackendT *bl)
		: backendLogger_(bl), curBuffer_(std::move(bl->acquire()))
{
}

template <typename BackendT>
ThreadLogger<BackendT>::~ThreadLogger()
{
	if (curBuffer_ && backendLogger_)
		backendLogger_->submit(std::move(curBuffer_));
}

template <typename BackendT>
void ThreadLogger<BackendT>::handoff(bool force)
{
	if (!curBuffer_)
	{
		curBuffer_ = backendLogger_->acquire();
		return;
	}

	backendLogger_->submit(std::move(curBuffer_));
	curBuffer_ = backendLogger_->acquire();
}