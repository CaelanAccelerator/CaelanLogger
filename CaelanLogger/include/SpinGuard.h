#pragma once
#include <atomic>
#include <thread>

struct SpinGuard
{
	std::atomic_flag &spinlockPen_;
	explicit SpinGuard(std::atomic_flag &lock) : spinlockPen_(lock)
	{
		while (spinlockPen_.test_and_set(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
	}

	~SpinGuard()
	{
		spinlockPen_.clear(std::memory_order_release);
	}
};