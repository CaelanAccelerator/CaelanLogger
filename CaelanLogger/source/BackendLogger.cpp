#include "BackendLogger.h"
#include "SpinGuard.h"
#include "TimeUtil.h"
#include <iostream>
#include <assert.h>

BackendLogger::BackendLogger(size_t bufSize, std::string dir) : futil_(std::make_unique<FileUtil>(dir))
{
	for (size_t i = 0; i < QUEUE_SIZE; i++)
	{
		freeQue_[i] = new Buffer(bufSize);
	}
	freeQueTail_ = 0;
	freeQueSize_ = QUEUE_SIZE;
	freeAvailable_.store(freeQueSize_ > 0, std::memory_order_release);
}

BackendLogger::~BackendLogger()
{
	stop();
	for (size_t i = 0; i < freeQueSize_; i++)
	{
		delete freeQue_[(freeQueHead_ + i) % QUEUE_SIZE];
	}
	freeQueSize_ = 0;
}

void BackendLogger::start()
{
	bool expected = false;
	if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
	{
		return; // already running_
	}
	writer_ = std::thread(&BackendLogger::run, this);
}

void BackendLogger::stop()
{
	running_.store(false, std::memory_order_release);
	if (writer_.joinable())
		writer_.join();
	futil_->roll();
}

void BackendLogger::run()
{
	while (running_.load(std::memory_order_acquire))
	{
		if (pendingQueSize_.load(std::memory_order_relaxed) == 0)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(100));
			continue;
		}
		write();
	}

	// drain? finish pending queue before ending
	while (true)
	{
		bool empty = false;
		{
			SpinGuard g(spinlock_);
			empty = (pendingQueSize_.load(std::memory_order_relaxed) == 0);
		}
		if (empty)
			break;
		write();
	}
}

void BackendLogger::submitAndAcquire(Buffer *&fullBuffer)
{
	if (!fullBuffer)
		return;

	SpinGuard guard(spinlock_);
	assert(pendingQueSize_.load(std::memory_order_relaxed) + freeQueSize_ <= QUEUE_SIZE);
	pendingQue_[pendingQueTail_] = fullBuffer;
	pendingQueTail_ = (pendingQueTail_ + 1) % QUEUE_SIZE;
	pendingQueSize_.fetch_add(1, std::memory_order_relaxed);

	if (freeQueSize_ < 1)
	{
		fullBuffer = nullptr;
		freeAvailable_.store(false, std::memory_order_release);
		return;
	}
	fullBuffer = freeQue_[freeQueHead_];
	freeQueHead_ = (freeQueHead_ + 1) % QUEUE_SIZE;
	--freeQueSize_;
}

void BackendLogger::write()
{
	size_t numBuf{0};
	Buffer *buffer[QUEUE_SIZE];
	{
		SpinGuard guard(spinlock_);

		if (pendingQueSize_.load(std::memory_order_relaxed) == 0)
			return;

		while (pendingQueSize_.load(std::memory_order_relaxed))
		{
			buffer[numBuf++] = pendingQue_[pendingQueHead_];
			pendingQueHead_ = (pendingQueHead_ + 1) % QUEUE_SIZE;
			pendingQueSize_.fetch_sub(1, std::memory_order_relaxed);
		}
	}
	for (size_t i = 0; i < numBuf; i++)
	{
		const char *data = buffer[i]->getBuffer();
		size_t size = buffer[i]->getSize();
		futil_->append(data, size);
		buffer[i]->reset();
	}
	assert(freeQueSize_ + numBuf <= QUEUE_SIZE);
	{
		SpinGuard guard(spinlock_);
		for (size_t i = 0; i < numBuf; i++)
		{
			freeQue_[freeQueTail_] = buffer[i];
			freeQueTail_ = (freeQueTail_ + 1) % QUEUE_SIZE;
		}
		freeQueSize_ += numBuf;
		freeAvailable_.store(true, std::memory_order_release);
		assert(freeQueSize_ <= QUEUE_SIZE);
	}
}

Buffer *BackendLogger::get_free_buffer()
{
	SpinGuard guard(spinlock_);
	if (freeQueSize_ < 1)
	{
		return nullptr;
	}
	Buffer *buf = freeQue_[freeQueHead_];
	freeQueHead_ = (freeQueHead_ + 1) % QUEUE_SIZE;
	--freeQueSize_;
	freeAvailable_.store(freeQueSize_ > 0, std::memory_order_release);
	return buf;
}

void BackendLogger::restart(size_t bufSize)
{
	stop();

	{
		SpinGuard guard(spinlock_);

		pendingQueHead_ = 0;
		pendingQueTail_ = 0;
		pendingQueSize_.store(0, std::memory_order_relaxed);

		for (size_t i = 0; i < freeQueSize_; ++i)
		{
			size_t idx = (freeQueHead_ + i) % QUEUE_SIZE;
			if (freeQue_[idx])
			{
				freeQue_[idx]->reset();
			}
		}

		Buffer *tmp[QUEUE_SIZE];
		size_t size = freeQueSize_;

		for (size_t i = 0; i < size; ++i)
		{
			tmp[i] = freeQue_[(freeQueHead_ + i) % QUEUE_SIZE];
		}

		for (size_t i = 0; i < size; ++i)
		{
			freeQue_[i] = tmp[i];
		}

		for (size_t i = size; i < QUEUE_SIZE; ++i)
		{
			freeQue_[i] = nullptr;
		}

		freeQueHead_ = 0;
		freeQueTail_ = size % QUEUE_SIZE;
		freeAvailable_.store(freeQueSize_ > 0, std::memory_order_release);
	}

	futil_ = std::make_unique<FileUtil>();
	start();
}
