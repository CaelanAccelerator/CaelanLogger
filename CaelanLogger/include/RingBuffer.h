#pragma once
#include "Buffer.h"
#include <atomic>
#include <optional>

#if defined(__cpp_lib_hardware_interference_size)
#include <new>
inline constexpr size_t kCacheLine = std::hardware_destructive_interference_size;
#else
inline constexpr size_t kCacheLine = 64;
#endif

// Rounds n up to the next power of two so it can be used as a ring buffer
// capacity with bitmask indexing (mask_ = capacity_ - 1). n == 0 rounds to 1.
constexpr size_t nextPow2(size_t n)
{
  if (n <= 1)
  {
    return 1;
  }

  n -= 1;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  return n + 1;
}

// Storage + push/pop dispatch shared by every ring buffer flavor, regardless
// of whether head_/tail_ need to be visible across threads.
template <class Derived, typename T>
class RingBuffer
{
protected:
  size_t capacity_{0};
  size_t mask_{0};
  std::unique_ptr<T[]> ringBuffer_;

public:
  explicit RingBuffer(size_t bufferSize);

  bool push(T &&elem);
  bool push(const T &elem);
  std::optional<T> pop();
  size_t capacity() const { return capacity_; }
};

template <class Derived, typename T>
RingBuffer<Derived, T>::RingBuffer(size_t bufferSize)
    : capacity_(nextPow2(bufferSize)), mask_(capacity_ - 1), ringBuffer_(std::make_unique<T[]>(capacity_))
{
}

template <class Derived, typename T>
bool RingBuffer<Derived, T>::push(const T &elem)
{
  return static_cast<Derived *>(this)->pushImpl(elem);
}

template <class Derived, typename T>
bool RingBuffer<Derived, T>::push(T &&elem)
{
  return static_cast<Derived *>(this)->pushImpl(std::move(elem));
}

template <class Derived, typename T>
std::optional<T> RingBuffer<Derived, T>::pop()
{
  return static_cast<Derived *>(this)->popImpl();
}

// head_/tail_ are written by one thread and read by another (producer thread
// vs. consumer thread), so they need atomics: not for mutual exclusion, but
// so the index update has release/acquire ordering with the element it
// guards, and so the cross-thread access isn't a plain data race.
template <class Derived, typename T>
class ConcurrentRingBuffer : public RingBuffer<Derived, T>
{
protected:
  alignas(kCacheLine) std::atomic<size_t> head_{0};
  alignas(kCacheLine) std::atomic<size_t> tail_{0};

public:
  using RingBuffer<Derived, T>::RingBuffer;

  const T &Front() const { return this->ringBuffer_[tail_.load(std::memory_order_relaxed) & this->mask_]; }
  bool isFull() const { return head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_relaxed) == this->capacity_; }
  bool isEmpty() const { return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed); }
  void reset()
  {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }
};

// Single-thread use only (same thread does both push and pop): no other
// thread ever touches head_/tail_, so plain counters are enough and atomics
// would be pure overhead.
template <class Derived, typename T>
class LocalRingBuffer : public RingBuffer<Derived, T>
{
protected:
  size_t head_{0};
  size_t tail_{0};

public:
  using RingBuffer<Derived, T>::RingBuffer;

  const T &Front() const { return this->ringBuffer_[tail_ & this->mask_]; }
  bool isFull() const { return head_ - tail_ == this->capacity_; }
  bool isEmpty() const { return head_ == tail_; }
  size_t size() const { return head_ - tail_; }
  void reset()
  {
    head_ = 0;
    tail_ = 0;
  }
};
