#include "BackendLogger.h"
#include "SpinGuard.h"
#include "TimeUtil.h"

BackendLogger::BackendLogger(size_t bufSize, size_t queueSize, std::string dir) : queueSize_(queueSize), futil_(std::make_unique<FileUtil>(dir))
{
	pendingQue_ = std::make_unique<std::unique_ptr<Buffer>[]>(queueSize_);
	freeQue_ = std::make_unique<std::unique_ptr<Buffer>[]>(queueSize_);
	for (size_t i = 0; i < queueSize_; i++)
	{
		freeQue_[i] = std::make_unique<Buffer>(bufSize);
	}
	freeQueTail_ = 0;
	freeQueSize_ = queueSize_;
	freeAvailable_.store(freeQueSize_ > 0, std::memory_order_release);
}

BackendLogger::~BackendLogger()
{
	stop();
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

std::unique_ptr<Buffer> BackendLogger::submitAndAcquire(std::unique_ptr<Buffer> fullBuffer)
{
	if (!fullBuffer)
		return nullptr;
	{
		SpinGuard guard(spinlockPen_);
		pendingQue_[pendingQueTail_] = std::move(fullBuffer);
		pendingQueTail_ = (pendingQueTail_ + 1) % queueSize_;
		pendingQueSize_.fetch_add(1, std::memory_order_relaxed);
		cv_.notify_one();
	}

	std::unique_ptr<Buffer> freeBuffer;
	{
		SpinGuard guard(spinlockFree_);
		if (freeQueSize_ < 1)
		{
			freeAvailable_.store(false, std::memory_order_release);
			return nullptr;
		}
		freeBuffer = std::move(freeQue_[freeQueHead_]);
		freeQueHead_ = (freeQueHead_ + 1) % queueSize_;
		--freeQueSize_;
	}
	freeAvailable_.store(freeQueSize_ > 0, std::memory_order_release);
	return freeBuffer;
}

void BackendLogger::write()
{
	size_t numBuf{0};
	std::vector<std::unique_ptr<Buffer>> buffer;
	buffer.resize(queueSize_);
	{
		SpinGuard guard(spinlockPen_);

		if (pendingQueSize_.load(std::memory_order_relaxed) == 0)
			return;

		while (pendingQueSize_.load(std::memory_order_relaxed))
		{
			buffer[numBuf++] = std::move(pendingQue_[pendingQueHead_]);
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
			freeQue_[freeQueTail_] = std::move(buffer[i]);
			freeQueTail_ = (freeQueTail_ + 1) % queueSize_;
			freeQueSize_++;
		}
		freeAvailable_.store(true, std::memory_order_release);
	}
}

std::unique_ptr<Buffer> BackendLogger::get_free_buffer()
{
	SpinGuard guard(spinlockFree_);
	if (freeQueSize_ < 1)
		return nullptr;

	std::unique_ptr<Buffer> freeBuffer = std::move(freeQue_[freeQueHead_]);
	freeQueHead_ = (freeQueHead_ + 1) % queueSize_;
	--freeQueSize_;
	freeAvailable_.store(freeQueSize_ > 0, std::memory_order_release);
	return freeBuffer;
}
