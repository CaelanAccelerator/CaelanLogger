#include "NormalWriter.h"
#include <cstring>

void NormalWriter::append(const char *data, size_t len)
{
  if (fd_ < 0)
  {
    if (!openFile(generateFileName()))
    {
      throw std::runtime_error(std::string("write() failed: ") + std::strerror(errno));
    }
  }
  if (shouldRoll(len))
  {
    roll();
  }
  size_t writtenDown = 0;
  // to prevent if write() doesn't finish.
  while (writtenDown < len)
  {
    ssize_t n = ::write(fd_, data + writtenDown, len - writtenDown);
    if (n > 0)
    {
      writtenDown += static_cast<size_t>(n);
      writtenBytes += n;
      continue;
    }
    if (n < 0 && errno == EINTR)
      continue; // retry
    // Other errors: disk full, permission, etc.
    // For now: throw or set an error flag.
    throw std::runtime_error(std::string("write() failed: ") + std::strerror(errno));
  }
}

void NormalWriter::writeDropMessage(char *msg, int len)
{
  write(fd_, msg, static_cast<size_t>(len));
  close(fd_);
  delete[] msg;
}