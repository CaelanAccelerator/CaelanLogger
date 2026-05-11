#include "BackendLogger.h"
#include "SpinGuard.h"
#include "TimeUtil.h"

BackendLogger::BackendLogger(size_t bufSize, size_t queueSize, std::string dir) : queueSize_(queueSize), futil_(std::make_unique<FileUtil>(dir))
{
	pendingQue_ = std::make_unique<Buffer *[]>(queueSize_);
	freeQue_ = std::make_unique<Buffer *[]>(queueSize_);
	for (size_t i = 0; i < queueSize_; i++)
	{
		freeQue_[i] = new Buffer(bufSize);
	}
	freeQueTail_ = 0;
	freeQueSize_ = queueSize_;
	freeAvailable_.store(freeQueSize_ > 0, std::memory_order_release);
}

BackendLogger::~BackendLogger()
{
	stop();
	for (size_t i = freeQueHead_; i < freeQueHead_ + freeQueSize_; i++)
	{
		delete freeQue_[i % queueSize_];
	}
	freeQueSize_ = 0;
}

void BackendLogger::record_drop()
{
	futil_->add_dropped();
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
	{
		// add lock to prevent notify loss
		/*
		Writer: pred() → false（running_=true）← still holding lock
																					stop(): running_=false（write, since no lock）
																					stop(): notify_all()  ← Writer is not in wait，loss the notify！
		Writer: unlock + enter wait              ← block forever
		*/
		std::lock_guard<std::mutex> lock(cvMutex_);
		running_.store(false, std::memory_order_release);
	}
	cv_.notify_all();
	if (writer_.joinable())
		writer_.join();
	futil_->roll();
}

void BackendLogger::run()
{
	while (true)
	{
		{
			std::unique_lock<std::mutex> lock(cvMutex_);
			auto predicate = [this]
			{
				return pendingQueSize_.load(std::memory_order_relaxed) > 0 || !running_.load(std::memory_order_acquire);
			};
			cv_.wait(lock, predicate);
		}
		if (!running_.load(std::memory_order_acquire) && pendingQueSize_.load(std::memory_order_relaxed) == 0)
			break;
		write();
	}

	// drain: finish any remaining pending buffers before exiting
	while (pendingQueSize_.load(std::memory_order_relaxed) > 0)
		write();
}

void BackendLogger::submitAndAcquire(Buffer *&fullBuffer)
{
	if (!fullBuffer)
		return;
	{
		SpinGuard guard(spinlockPen_);
		pendingQue_[pendingQueTail_] = fullBuffer;
		pendingQueTail_ = (pendingQueTail_ + 1) % queueSize_;
		pendingQueSize_.fetch_add(1, std::memory_order_relaxed);
		cv_.notify_one();
	}

	{
		SpinGuard guard(spinlockFree_);
		if (freeQueSize_ < 1)
		{
			fullBuffer = nullptr;
			freeAvailable_.store(false, std::memory_order_release);
			return;
		}
		fullBuffer = freeQue_[freeQueHead_];
		freeQueHead_ = (freeQueHead_ + 1) % queueSize_;
		--freeQueSize_;
	}
	freeAvailable_.store(freeQueSize_ > 0, std::memory_order_release);
}

void BackendLogger::write()
{
	size_t numBuf{0};
	std::vector<Buffer *> buffer;
	buffer.resize(queueSize_);
	{
		SpinGuard guard(spinlockPen_);

		if (pendingQueSize_.load(std::memory_order_relaxed) == 0)
			return;

		while (pendingQueSize_.load(std::memory_order_relaxed))
		{
			buffer[numBuf++] = pendingQue_[pendingQueHead_];
			pendingQueHead_ = (pendingQueHead_ + 1) % queueSize_;
			pendingQueSize_.fetch_sub(1, std::memory_order_relaxed);
		}
	}
	for (size_t i = 0; i < numBuf; i++)
	{
		const char *data = buffer[i]->getBuffer();
		size_t size = buffer[i]->getSize();
		futil_->append(data, size);
		buffer[i]->reset();
		{
			SpinGuard guard(spinlockFree_);
			freeQue_[freeQueTail_] = buffer[i];
			freeQueTail_ = (freeQueTail_ + 1) % queueSize_;
			freeQueSize_++;
		}
		freeAvailable_.store(true, std::memory_order_release);
	}
}

Buffer *BackendLogger::get_free_buffer()
{
	SpinGuard guard(spinlockFree_);
	if (freeQueSize_ < 1)
		return nullptr;

	Buffer *buf = freeQue_[freeQueHead_];
	freeQueHead_ = (freeQueHead_ + 1) % queueSize_;
	--freeQueSize_;
	freeAvailable_.store(freeQueSize_ > 0, std::memory_order_release);
	return buf;
}
