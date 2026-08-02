#include "UringBackend.h"
#include <errno.h>
#include <stdexcept>

UringBackend::UringBackend(size_t bufferSize, size_t poolCapacity, std::string dir)
    : FileUtil<UringBackend>(dir),
      poolCapacity_(poolCapacity),
      freeIdxes_(std::make_unique<LocalQueue<size_t>>(poolCapacity)),
      bufferPool_(std::make_unique<std::unique_ptr<Buffer>[]>(poolCapacity))
{
  for (size_t i = 0; i < poolCapacity; i++)
  {
    bufferPool_[i] = std::make_unique<Buffer>(bufferSize);
    bufferPool_[i]->setIdx(i);
    freeIdxes_->push(i);
  }

  int ret = io_uring_queue_init(poolCapacity, &ring_, 0);
  if (ret < 0)
  {
    // TO DO
  }
}

UringBackend::~UringBackend()
{
  stop();
  io_uring_queue_exit(&ring_);
}

void UringBackend::stop()
{
  if (stopped_)
  {
    return; // already stopped
  }
  stopped_ = true;

  roll();

  // drain in-flight writes
  while (freeIdxes_->size() != poolCapacity_ || msgBuffer_ != nullptr)
  {
    io_uring_cqe *cqe = nullptr;
    int ret = io_uring_wait_cqe(&ring_, &cqe);
    if (ret < 0)
    {
      break;
    }
    reapCompletion(cqe);
  }
}

void UringBackend::record_drop()
{
  add_dropped();
}

void UringBackend::writeDropMessage(char *msg, int len)
{
  msgBuffer_ = msg;
  pendingCloseFd_ = fd_; // remember which fd this message actually belongs to;
                         // fd_ itself will be reassigned to the new file right after this call
  io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
  io_uring_prep_write(sqe, pendingCloseFd_, msg, len, -1);
  io_uring_sqe_set_data(sqe, reinterpret_cast<void *>(msg));
  io_uring_submit(&ring_);
}

void UringBackend::reapCompletion(io_uring_cqe *cqe)
{
  if (msgBuffer_ != nullptr && reinterpret_cast<char *>(cqe->user_data) == msgBuffer_)
  {
    delete[] msgBuffer_;
    msgBuffer_ = nullptr;
    if (pendingCloseFd_ >= 0)
    {
      close(pendingCloseFd_);
      pendingCloseFd_ = -1;
    }
  }
  else
  {
    size_t idx = static_cast<size_t>(cqe->user_data);
    bufferPool_[idx]->reset();
    freeIdxes_->push(idx);
  }
  io_uring_cqe_seen(&ring_, cqe);
}

void UringBackend::submit(std::unique_ptr<Buffer> fullBuffer)
{
  // for initilization and open the file
  if (fd_ < 0)
  {
    if (!openFile(generateFileName()))
    {
      throw std::runtime_error(std::string("write() failed: ") + std::strerror(errno));
    }
  }

  // return back the fullBuffer to its slot
  size_t bufSize = fullBuffer->size();
  size_t idxIn = fullBuffer->idx();
  bufferPool_[idxIn] = std::move(fullBuffer);

  // get completed buffer back
  io_uring_cqe *cqes[poolCapacity_];
  int numFinished = io_uring_peek_batch_cqe(&ring_, cqes, poolCapacity_);
  for (int i = 0; i < numFinished; i++)
  {
    reapCompletion(cqes[i]);
  }

  // if we write enough bytes, we roll and open a new file
  if (shouldRoll(bufSize))
  {
    roll();
  }

  // give the fullBuffer to io_uring
  io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
  io_uring_prep_write(sqe, fd_, bufferPool_[idxIn]->getBuffer(), bufferPool_[idxIn]->size(), -1);
  io_uring_sqe_set_data(sqe, reinterpret_cast<void *>(idxIn)); // we just need to know the idx of Completed buffer to recycle it
  io_uring_submit(&ring_);
}

// return a free buffer to thread logger
std::unique_ptr<Buffer> UringBackend::acquire()
{
  auto maybeIdx = freeIdxes_->pop();
  if (!maybeIdx.has_value())
  {
    return nullptr;
  }
  size_t idxOut = maybeIdx.value();
  return std::move(bufferPool_[idxOut]);
}
