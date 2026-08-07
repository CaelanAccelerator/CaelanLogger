#include "SharedBackend.h"

SharedBackend::SharedBackend(size_t bufSize, size_t poolCapacity, std::string dir)
		: poolCapacity_(poolCapacity),
			submittedIdxes_(std::make_unique<MPSCSpinLockQueue<size_t>>(poolCapacity)),
			freeIdxes_(std::make_unique<SPMCSpinLockQueue<size_t>>(poolCapacity)),
			bufferPool_(std::make_unique<std::unique_ptr<Buffer>[]>(poolCapacity)),
			futil_(std::make_unique<NormalWriter>(dir))
{
	for (size_t i = 0; i < poolCapacity_; i++)
	{
		bufferPool_[i] = std::make_unique<Buffer>(bufSize);
		bufferPool_[i]->setIdx(i);
		freeIdxes_->push(i);
	}

	start();
}

SharedBackend::~SharedBackend()
{
	stop();
}

void SharedBackend::stop()
{
	// add lock to prevent notify loss
	/*
	Writer: pred() → false（running_=true）← still holding lock
																				stop(): running_=false（write, since no lock）
																				stop(): notify_all()  ← Writer is not in wait，loss the notify！
	Writer: unlock + enter wait              ← block forever
	*/
	{
		std::lock_guard<std::mutex> lock(cvMutex_);
		bool expected = true;
		if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
		{
			return; // already stopped
		}
	}

	cv_.notify_all();
	if (writer_.joinable())
		writer_.join();
	futil_->roll();
}

void SharedBackend::record_drop()
{
	futil_->add_dropped();
}

void SharedBackend::start()
{
	bool expected = false;
	if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
	{
		return; // already running_
	}
	writer_ = std::thread(&SharedBackend::run, this);
}

void SharedBackend::run()
{
	while (true)
	{
		{
			std::unique_lock<std::mutex> lock(cvMutex_);
			auto predicate = [this]
			{
				return !submittedIdxes_->isEmpty() || !running_.load(std::memory_order_acquire);
			};
			cv_.wait(lock, predicate);
		}
		if (!running_.load(std::memory_order_acquire) && submittedIdxes_->isEmpty())
			break;
		write();
	}

	// drain: finish any remaining pending buffers before exiting
	while (!submittedIdxes_->isEmpty())
		write();
}

void SharedBackend::submit(std::unique_ptr<Buffer> lastBuffer)
{
	if (!lastBuffer)
	{
		return;
	}

	size_t idxIn = lastBuffer->idx();
	bufferPool_[idxIn] = std::move(lastBuffer);
	submittedIdxes_->push(idxIn);
	cv_.notify_one();
}

std::unique_ptr<Buffer> SharedBackend::acquire()
{
	auto maybeIdx = freeIdxes_->pop();
	if (!maybeIdx.has_value())
	{
		return nullptr;
	}
	size_t idxOut = *maybeIdx;

	return std::move(bufferPool_[idxOut]);
}

void SharedBackend::write()
{
	size_t numBuf{0};
	size_t bufIdxes[poolCapacity_];

	while (true)
	{
		auto maybeIdx = submittedIdxes_->pop();
		if (!maybeIdx.has_value())
		{
			break;
		}
		size_t idx = *maybeIdx;

		bufIdxes[numBuf++] = idx;
	}

	for (size_t i = 0; i < numBuf; i++)
	{
		size_t bufIdx = bufIdxes[i];
		const char *data = bufferPool_[bufIdx]->getBuffer();
		size_t size = bufferPool_[bufIdx]->size();
		futil_->append(data, size);
		bufferPool_[bufIdx]->reset();
		freeIdxes_->push(bufIdx);
	}
}
