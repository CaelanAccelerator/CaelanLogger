#pragma once
#include "FileUtil.h"

class NormalWriter : public FileUtil<NormalWriter>
{
private:
  /* data */
public:
  using FileUtil<NormalWriter>::FileUtil;
  ~NormalWriter() = default;
  void append(const char *data, size_t len);
  void writeDropMessage(char *msg, int len);
};
