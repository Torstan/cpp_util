#include <fcntl.h>
#include <poll.h>

#include <iostream>
#include <stdexcept>

#include "conn_util/fd_notifier.h"
#include "conn_util/status.h"
#include "test_utils.h"

namespace {

using conn_util_test::Require;

bool IsNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  Require(flags >= 0, "fcntl F_GETFL succeeds");
  return (flags & O_NONBLOCK) != 0;
}

bool IsReadableNow(int fd) {
  pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  const int result = poll(&pfd, 1, 0);
  Require(result >= 0, "poll succeeds");
  return result > 0 && (pfd.revents & POLLIN) != 0;
}

bool BecomesReadable(int fd) {
  pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLIN;
  pfd.revents = 0;
  const int result = poll(&pfd, 1, 1000);
  Require(result >= 0, "poll with timeout succeeds");
  return result > 0 && (pfd.revents & POLLIN) != 0;
}

void RequireOk(const conn_util::Status& status, const std::string& message) {
  Require(status.ok(), message + ": " + status.message());
}

void TestNotifierStartsValidAndEmpty() {
  conn_util::FdNotifier notifier;
  Require(notifier.valid(), "notifier is valid");
  Require(notifier.readFd() >= 0, "read fd is exposed");
  Require(IsNonBlocking(notifier.readFd()), "read fd is non-blocking");

  Require(!IsReadableNow(notifier.readFd()), "new notifier starts empty");
  RequireOk(notifier.drain(), "drain empty notifier");
  Require(!IsReadableNow(notifier.readFd()), "empty notifier stays empty");
}

void TestNotifyMakesReadFdReadableAndDrainClearsIt() {
  conn_util::FdNotifier notifier;
  Require(notifier.valid(), "notifier is valid");

  RequireOk(notifier.notify(), "notify succeeds");
  Require(BecomesReadable(notifier.readFd()), "read fd becomes readable");

  RequireOk(notifier.drain(), "drain after notify");
  Require(!IsReadableNow(notifier.readFd()), "drain clears readability");
}

void TestRepeatedNotifyIsCoalescedAndDrained() {
  conn_util::FdNotifier notifier;
  Require(notifier.valid(), "notifier is valid");

  RequireOk(notifier.notify(), "first notify succeeds");
  RequireOk(notifier.notify(), "second notify succeeds");
  RequireOk(notifier.notify(), "third notify succeeds");
  Require(BecomesReadable(notifier.readFd()), "read fd is readable");

  RequireOk(notifier.drain(), "drain repeated notifications");
  Require(!IsReadableNow(notifier.readFd()), "drain clears all notifications");
}

}  // namespace

int main() {
  try {
    TestNotifierStartsValidAndEmpty();
    TestNotifyMakesReadFdReadableAndDrainClearsIt();
    TestRepeatedNotifyIsCoalescedAndDrained();
    std::cout << "fd_notifier_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
