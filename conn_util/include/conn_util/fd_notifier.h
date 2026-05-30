#pragma once

#include "conn_util/status.h"

namespace conn_util {

class FdNotifier {
public:
  FdNotifier();
  ~FdNotifier();

  FdNotifier(const FdNotifier&) = delete;
  FdNotifier& operator=(const FdNotifier&) = delete;

  bool valid() const;
  int readFd() const;
  Status notify();
  Status drain();

private:
  int read_fd_ = -1;
  int write_fd_ = -1;
};

}  // namespace conn_util
