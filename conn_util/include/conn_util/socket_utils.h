#pragma once

namespace conn_util {

class Endpoint;

int SetNonBlocking(int fd);
int SetTcpNoDelay(int fd);
int CreateTcpListenSocket(const Endpoint& endpoint, int backlog);
int CreateTcpClientSocket();

}  // namespace conn_util
