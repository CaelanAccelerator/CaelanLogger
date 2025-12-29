#include<atomic>
#include <thread>

struct SpinGuard
{
	std::atomic_flag& spinlock;
	explicit SpinGuard(std::atomic_flag& lock) : spinlock(lock)
	{
		while (spinlock.test_and_set(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
	}

	~SpinGuard()
	{
		spinlock.clear(std::memory_order_release);
	}
};