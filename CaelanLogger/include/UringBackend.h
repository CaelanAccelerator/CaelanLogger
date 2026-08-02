#pragma once
#include <string>
#include <cstring>
#include <memory>
#include <Buffer.h>
#include <liburing.h>

#include "FileUtil.h"
#include "LocalQueue.h"

class UringBackend : public FileUtil<UringBackend>
{
private:
  size_t poolCapacity_;
  std::unique_ptr<LocalQueue<size_t>> freeIdxes_;
  std::unique_ptr<std::unique_ptr<Buffer>[]> bufferPool_;
  io_uring ring_;
  char *msgBuffer_{nullptr};
  int pendingCloseFd_{-1};
  bool stopped_{false};

  // Shared by submit()'s peek loop and stop()'s drain loop: recognizes the
  // dropped-count message's cqe (by its unique msgBuffer_ user_data) and
  // frees/closes for it instead of recycling it as a buffer index.
  void reapCompletion(io_uring_cqe *cqe);

public:
  UringBackend(size_t bufSize, size_t queueSize, std::string dir = "./log");
  ~UringBackend();
  std::unique_ptr<Buffer> acquire();
  void submit(std::unique_ptr<Buffer>);
  void writeDropMessage(char *msg, int len);
  void record_drop();
  void stop();
};
