#pragma once
#include <memory>
#include <optional>
#include "Buffer.h"
#include "RingBuffer.h"

template <typename T>
class LocalQueue : public LocalRingBuffer<LocalQueue<T>, T>
{
public:
  using Base = LocalRingBuffer<LocalQueue<T>, T>;

  explicit LocalQueue(size_t bufferSize) : Base(bufferSize) {}

  bool pushImpl(T &&elem);
  bool pushImpl(const T &elem);
  std::optional<T> popImpl();
};

template <typename T>
bool LocalQueue<T>::pushImpl(T &&elem)
{
  if (this->isFull())
  {
    return false;
  }
  this->ringBuffer_[this->head_ & this->mask_] = std::move(elem);
  this->head_ += 1;
  return true;
}

template <typename T>
bool LocalQueue<T>::pushImpl(const T &elem)
{
  if (this->isFull())
  {
    return false;
  }
  this->ringBuffer_[this->head_ & this->mask_] = elem;
  this->head_ += 1;
  return true;
}

template <typename T>
std::optional<T> LocalQueue<T>::popImpl()
{
  if (this->isEmpty())
  {
    return std::nullopt;
  }
  T res = std::move(this->ringBuffer_[this->tail_ & this->mask_]);
  this->tail_ += 1;
  return res;
}
