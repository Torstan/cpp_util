#include "conn_util/endpoint.h"

#include <arpa/inet.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace conn_util {

bool Endpoint::parse(const std::string& text, Endpoint* out) {
  if (out == nullptr) {
    return false;
  }

  const std::size_t colon = text.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size()) {
    return false;
  }

  errno = 0;
  char* end = nullptr;
  const long port = std::strtol(text.c_str() + colon + 1, &end, 10);
  if (errno != 0 || end == nullptr || *end != '\0' || port <= 0 ||
      port > 65535) {
    return false;
  }

  *out = Endpoint(text.substr(0, colon), static_cast<std::uint16_t>(port));
  return true;
}

bool Endpoint::toSockAddr(sockaddr_in* out) const {
  if (out == nullptr) {
    return false;
  }

  std::memset(out, 0, sizeof(*out));
  out->sin_family = AF_INET;
  out->sin_port = htons(port_);

  if (host_ == "*" || host_ == "0" || host_ == "0.0.0.0") {
    out->sin_addr.s_addr = htonl(INADDR_ANY);
    return true;
  }

  return inet_pton(AF_INET, host_.c_str(), &out->sin_addr) == 1;
}

std::string Endpoint::toString() const {
  return host_ + ":" + std::to_string(port_);
}

}  // namespace conn_util
