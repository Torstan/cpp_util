#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string>

#include "conn_util/endpoint.h"
#include "conn_util/socket_utils.h"
#include "test_utils.h"

namespace {

using conn_util_test::Require;
using conn_util_test::RequireEqualInt;

class UniqueFd {
public:
  explicit UniqueFd(int fd = -1) : fd_(fd) {}
  ~UniqueFd() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

  UniqueFd(const UniqueFd&) = delete;
  UniqueFd& operator=(const UniqueFd&) = delete;

  int get() const { return fd_; }

private:
  int fd_;
};

bool IsNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  Require(flags >= 0, "fcntl F_GETFL succeeds");
  return (flags & O_NONBLOCK) != 0;
}

bool HasTcpNoDelay(int fd) {
  int value = 0;
  socklen_t value_len = sizeof(value);
  Require(getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &value, &value_len) == 0,
          "getsockopt TCP_NODELAY succeeds");
  return value != 0;
}

void TestSetNonBlocking() {
  int fds[2] = {-1, -1};
  Require(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0,
          "socketpair succeeds");
  UniqueFd left(fds[0]);
  UniqueFd right(fds[1]);

  Require(!IsNonBlocking(left.get()), "socket starts blocking");
  RequireEqualInt(conn_util::SetNonBlocking(left.get()), 0,
                  "SetNonBlocking returns success");
  Require(IsNonBlocking(left.get()), "socket becomes non-blocking");
}

void TestSetTcpNoDelay() {
  UniqueFd fd(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  Require(fd.get() >= 0, "create TCP socket");

  RequireEqualInt(conn_util::SetTcpNoDelay(fd.get()), 0,
                  "SetTcpNoDelay returns success");
  Require(HasTcpNoDelay(fd.get()), "TCP_NODELAY is enabled");
}

void TestCreateTcpClientSocket() {
  UniqueFd fd(conn_util::CreateTcpClientSocket());
  Require(fd.get() >= 0, "CreateTcpClientSocket succeeds");
  Require(IsNonBlocking(fd.get()), "client socket is non-blocking");
  Require(HasTcpNoDelay(fd.get()), "client socket has TCP_NODELAY");
}

void TestCreateTcpListenSocket() {
  conn_util::Endpoint endpoint("127.0.0.1", 0);
  UniqueFd fd(conn_util::CreateTcpListenSocket(endpoint, 16));
  Require(fd.get() >= 0, "CreateTcpListenSocket succeeds");
  Require(IsNonBlocking(fd.get()), "listen socket is non-blocking");

  sockaddr_in addr;
  socklen_t addr_len = sizeof(addr);
  Require(getsockname(fd.get(), reinterpret_cast<sockaddr*>(&addr),
                      &addr_len) == 0,
          "getsockname succeeds");
  RequireEqualInt(addr.sin_family, AF_INET, "listener family");
  Require(ntohs(addr.sin_port) != 0, "ephemeral listener has assigned port");

  int accepting = 0;
  socklen_t accepting_len = sizeof(accepting);
  Require(getsockopt(fd.get(), SOL_SOCKET, SO_ACCEPTCONN, &accepting,
                     &accepting_len) == 0,
          "getsockopt SO_ACCEPTCONN succeeds");
  Require(accepting != 0, "listener is accepting connections");
}

void TestCreateTcpListenSocketRejectsInvalidInput() {
  conn_util::Endpoint invalid_host("not-an-ip", 12345);
  UniqueFd invalid_fd(conn_util::CreateTcpListenSocket(invalid_host, 16));
  RequireEqualInt(invalid_fd.get(), -1, "invalid listen host is rejected");

  conn_util::Endpoint loopback("127.0.0.1", 0);
  UniqueFd invalid_backlog(conn_util::CreateTcpListenSocket(loopback, 0));
  RequireEqualInt(invalid_backlog.get(), -1, "zero backlog is rejected");
}

}  // namespace

int main() {
  try {
    TestSetNonBlocking();
    TestSetTcpNoDelay();
    TestCreateTcpClientSocket();
    TestCreateTcpListenSocket();
    TestCreateTcpListenSocketRejectsInvalidInput();
    std::cout << "socket_utils_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
