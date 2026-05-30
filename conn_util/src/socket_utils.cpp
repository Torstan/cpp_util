#include "conn_util/socket_utils.h"

#include "conn_util/endpoint.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

namespace conn_util {

int SetNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return -1;
  }
  if ((flags & O_NONBLOCK) != 0) {
    return 0;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int SetTcpNoDelay(int fd) {
  int flag = 1;
  return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

int CreateTcpListenSocket(const Endpoint& endpoint, int backlog) {
  if (backlog <= 0) {
    errno = EINVAL;
    return -1;
  }

  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    return -1;
  }

  int reuse = 1;
  sockaddr_in addr;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0 ||
      !endpoint.toSockAddr(&addr) ||
      bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      listen(fd, backlog) != 0 || SetNonBlocking(fd) != 0) {
    close(fd);
    return -1;
  }

  return fd;
}

int CreateTcpClientSocket() {
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    return -1;
  }

  if (SetNonBlocking(fd) != 0 || SetTcpNoDelay(fd) != 0) {
    close(fd);
    return -1;
  }

  return fd;
}

}  // namespace conn_util
