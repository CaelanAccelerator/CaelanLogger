#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <ThreadLogger.h>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include "SpinGuard.h"
#include "NormalWriter.h"
#include "Buffer.h"
#include "RingBuffer.h"

template <typename T>
class MPSCSpinLockQueue : public ConcurrentRingBuffer<MPSCSpinLockQueue<T>, T>
{
public:
  using Base = ConcurrentRingBuffer<MPSCSpinLockQueue<T>, T>;

  explicit MPSCSpinLockQueue(size_t bufferSize) : Base(bufferSize) {}

  bool pushImpl(T &&elem);
  bool pushImpl(const T &elem);
  std::optional<T> popImpl();

private:
  std::mutex cvMutex_;
  std::condition_variable cv_;
  std::atomic_flag spinlock_ = ATOMIC_FLAG_INIT;
};

template <typename T>
bool MPSCSpinLockQueue<T>::pushImpl(T &&elem)
{
  SpinGuard guard(spinlock_);
  size_t h = this->head_.load(std::memory_order_relaxed);
  if (h - this->tail_.load(std::memory_order_acquire) == this->capacity_)
  {
    return false;
  }
  this->ringBuffer_[h & this->mask_] = std::move(elem);
  this->head_.store(h + 1, std::memory_order_release);
  return true;
}

template <typename T>
bool MPSCSpinLockQueue<T>::pushImpl(const T &elem)
{
  SpinGuard guard(spinlock_);
  size_t h = this->head_.load(std::memory_order_relaxed);
  if (h - this->tail_.load(std::memory_order_acquire) == this->capacity_)
  {
    return false;
  }
  this->ringBuffer_[h & this->mask_] = elem;
  this->head_.store(h + 1, std::memory_order_release);
  return true;
}

template <typename T>
std::optional<T> MPSCSpinLockQueue<T>::popImpl()
{
  size_t t = this->tail_.load(std::memory_order_relaxed);
  if (this->head_.load(std::memory_order_acquire) == t)
  {
    return std::nullopt;
  }
  T res = std::move(this->ringBuffer_[this->tail_.load(std::memory_order_relaxed) & this->mask_]);
  this->tail_.store(t + 1, std::memory_order_release);
  return res;
}