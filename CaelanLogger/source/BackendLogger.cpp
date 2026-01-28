#include "BackendLogger.h"
#include "SpinGuard.h"
#include "TimeUtil.h"
#include <iostream>

BackendLogger::BackendLogger(size_t bufSize, std::string dir) : futil(std::make_unique<FileUtil>()) {
	for (size_t i = 0; i < QUEUE_SIZE; i++)
	{
		freeQue[i] = new Buffer(bufSize);
	}
	freeQueBack = QUEUE_SIZE - 1;
	freeQueSize = QUEUE_SIZE;
	freeAvailable.store(freeQueSize > 0, std::memory_order_release);
}  

BackendLogger::~BackendLogger() {
	stop();
}

//void BackendLogger::start() {
//	running.store(true, std::memory_order_release);
//	writer = std::thread(&BackendLogger::run, this);
//}

void BackendLogger::start() {
	if (writer.joinable()) {
		std::cerr << "[BUG] start() called while writer.joinable()==true\n";
		std::terminate();
	}
	running.store(true, std::memory_order_release);
	writer = std::thread(&BackendLogger::run, this);
}

void BackendLogger::stop() {
	running.store(false, std::memory_order_release);
	if (writer.joinable()) writer.join();
}


void BackendLogger::run() {
	while (running.load(std::memory_order_acquire)) {		
		write();
	}

	// drain? finish pending queue before ending
	while (true) {
		bool empty = false;
		{
			SpinGuard g(spinlock);
			empty = (pendingQueSize == 0);
		}
		if (empty) break;
		write();
	}
}

void BackendLogger::submit_and_acquire(Buffer*& fullBuffer)
{
	if (!fullBuffer) {
		return;
	}
	SpinGuard guard(spinlock);
	if (pendingQueSize >= QUEUE_SIZE) {
		fullBuffer->reset();
		return;
	}
	pendingQue[pendingQueBack] = fullBuffer;
	pendingQueBack = (pendingQueBack + 1) % QUEUE_SIZE; 
	++pendingQueSize; 
	
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
	size_t numBuf{ 0 };
	Buffer* buffer[QUEUE_SIZE];
	{
		SpinGuard guard(spinlock);
		if (pendingQueSize == 0)
		{
			return;
		}
		while (pendingQueSize)
		{
			buffer[numBuf++] = pendingQue[pendingQueFront];
			pendingQueFront = (pendingQueFront + 1) % QUEUE_SIZE;
			--pendingQueSize;
		}
	}
	for (size_t i = 0; i < numBuf; i++)
	{
		const char* data = buffer[i]->getBuffer();
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

void BackendLogger::restart(size_t bufSize)
{
	futil->~FileUtil();
	new (futil.get()) FileUtil();
	for (size_t i = 0; i < QUEUE_SIZE; i++)
	{
		freeQue[i] = new Buffer(bufSize);
	}
	freeQueBack = QUEUE_SIZE - 1;
	freeQueSize = QUEUE_SIZE;
	freeAvailable.store(freeQueSize > 0, std::memory_order_release);
}
