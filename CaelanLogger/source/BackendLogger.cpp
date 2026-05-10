#include "BackendLogger.h"
#include "SpinGuard.h"
#include "TimeUtil.h"
#include <iostream>
#include <assert.h>

BackendLogger::BackendLogger(size_t bufSize, std::string dir) : futil(std::make_unique<FileUtil>(dir))
{
	for (size_t i = 0; i < QUEUE_SIZE; i++)
	{
		freeQue[i] = new Buffer(bufSize);
	}
	freeQueBack = 0;
	freeQueSize = QUEUE_SIZE;
	freeAvailable.store(freeQueSize > 0, std::memory_order_release);
}

BackendLogger::~BackendLogger()
{
	stop();
	for (size_t i = 0; i < freeQueSize; i++)
	{
		delete freeQue[(freeQueFront + i) % QUEUE_SIZE];
	}
	freeQueSize = 0;
}

void BackendLogger::start()
{
	bool expected = false;
	if (!running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
	{
		return; // already running
	}
	writer = std::thread(&BackendLogger::run, this);
}

void BackendLogger::stop()
{
	running.store(false, std::memory_order_release);
	if (writer.joinable())
		writer.join();
}

void BackendLogger::run()
{
	while (running.load(std::memory_order_acquire))
	{
		if (pendingQueSize.load(std::memory_order_relaxed) == 0)
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
			SpinGuard g(spinlock);
			empty = (pendingQueSize.load(std::memory_order_relaxed) == 0);
		}
		if (empty)
			break;
		write();
	}
}

void BackendLogger::submit_and_acquire(Buffer *&fullBuffer)
{
	if (!fullBuffer)
	{
		return;
	}
	SpinGuard guard(spinlock);
	if (pendingQueSize.load(std::memory_order_relaxed) >= QUEUE_SIZE)
	{
		fullBuffer->reset();
		return;
	}
	pendingQue[pendingQueBack] = fullBuffer;
	pendingQueBack = (pendingQueBack + 1) % QUEUE_SIZE;
	pendingQueSize.fetch_add(1, std::memory_order_relaxed);

	if (freeQueSize < 1)
	{
		fullBuffer = nullptr;
		freeAvailable.store(false, std::memory_order_release);
		return;
	}
	fullBuffer = freeQue[freeQueFront];
	freeQueFront = (freeQueFront + 1) % QUEUE_SIZE;
	--freeQueSize;
}

void BackendLogger::write()
{
	size_t numBuf{0};
	Buffer *buffer[QUEUE_SIZE];
	{
		SpinGuard guard(spinlock);
		if (pendingQueSize.load(std::memory_order_relaxed) == 0)
		{
			return;
		}
		while (pendingQueSize.load(std::memory_order_relaxed))
		{
			buffer[numBuf++] = pendingQue[pendingQueFront];
			pendingQueFront = (pendingQueFront + 1) % QUEUE_SIZE;
			pendingQueSize.fetch_sub(1, std::memory_order_relaxed);
		}
	}
	for (size_t i = 0; i < numBuf; i++)
	{
		const char *data = buffer[i]->getBuffer();
		size_t size = buffer[i]->getSize();
		futil->append(data, size);
		buffer[i]->reset();
	}
	{
		SpinGuard guard(spinlock);
		for (size_t i = 0; i < numBuf; i++)
		{
			freeQue[freeQueBack] = buffer[i];
			freeQueBack = (freeQueBack + 1) % QUEUE_SIZE;
		}
		freeQueSize += numBuf;
		freeAvailable.store(true, std::memory_order_release);
	}
}

Buffer *BackendLogger::get_free_buffer()
{
	SpinGuard guard(spinlock);
	if (freeQueSize < 1)
	{
		return nullptr;
	}
	Buffer *buf = freeQue[freeQueFront];
	freeQueFront = (freeQueFront + 1) % QUEUE_SIZE;
	--freeQueSize;
	freeAvailable.store(freeQueSize > 0, std::memory_order_release);
	return buf;
}

void BackendLogger::restart(size_t bufSize)
{
	stop();

	{
		SpinGuard guard(spinlock);

		pendingQueFront = 0;
		pendingQueBack = 0;
		pendingQueSize.store(0, std::memory_order_relaxed);

		for (size_t i = 0; i < freeQueSize; ++i)
		{
			size_t idx = (freeQueFront + i) % QUEUE_SIZE;
			if (freeQue[idx])
			{
				freeQue[idx]->reset();
			}
		}

		Buffer *tmp[QUEUE_SIZE];
		size_t size = freeQueSize;

		for (size_t i = 0; i < size; ++i)
		{
			tmp[i] = freeQue[(freeQueFront + i) % QUEUE_SIZE];
		}

		for (size_t i = 0; i < size; ++i)
		{
			freeQue[i] = tmp[i];
		}

		for (size_t i = size; i < QUEUE_SIZE; ++i)
		{
			freeQue[i] = nullptr;
		}

		freeQueFront = 0;
		freeQueBack = size % QUEUE_SIZE;
		freeAvailable.store(freeQueSize > 0, std::memory_order_release);
	}

	futil = std::make_unique<FileUtil>();
	start();
}
