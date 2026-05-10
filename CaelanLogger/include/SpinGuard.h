#pragma once
#include <atomic>
#include <thread>

struct SpinGuard
{
	std::atomic_flag &spinlock_;
	explicit SpinGuard(std::atomic_flag &lock) : spinlock_(lock)
	{
		while (spinlock_.test_and_set(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
	}

	~SpinGuard()
	{
		spinlock_.clear(std::memory_order_release);
	}
};