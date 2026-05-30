#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <netinet/in.h>

namespace conn_util {

class Endpoint {
public:
  Endpoint() = default;
  Endpoint(std::string host, std::uint16_t port)
      : host_(std::move(host)), port_(port) {}

  static bool parse(const std::string& text, Endpoint* out);
  bool toSockAddr(sockaddr_in* out) const;

  const std::string& host() const { return host_; }
  std::uint16_t port() const { return port_; }
  std::string toString() const;

private:
  std::string host_ = "127.0.0.1";
  std::uint16_t port_ = 0;
};

}  // namespace conn_util
