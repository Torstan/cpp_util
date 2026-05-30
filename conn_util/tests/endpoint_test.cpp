#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "conn_util/endpoint.h"
#include "conn_util/status.h"
#include "test_utils.h"

namespace {

using conn_util_test::Require;
using conn_util_test::RequireEqual;
using conn_util_test::RequireEqualInt;

void TestStatusBasics() {
  conn_util::Status ok = conn_util::Status::Ok();
  Require(ok.ok(), "default status is ok");
  Require(ok.code() == conn_util::StatusCode::kOk, "ok status code");
  Require(ok.message().empty(), "ok status message is empty");

  conn_util::Status invalid =
      conn_util::Status::InvalidArgument("bad endpoint");
  Require(!invalid.ok(), "invalid status is not ok");
  Require(invalid.code() == conn_util::StatusCode::kInvalidArgument,
          "invalid status code");
  RequireEqual(invalid.message(), "bad endpoint", "invalid status message");
}

void TestParseValidEndpoint() {
  conn_util::Endpoint endpoint;
  Require(conn_util::Endpoint::parse("127.0.0.1:6379", &endpoint),
          "parse valid endpoint");
  RequireEqual(endpoint.host(), "127.0.0.1", "parsed host");
  RequireEqualInt(endpoint.port(), 6379, "parsed port");
  RequireEqual(endpoint.toString(), "127.0.0.1:6379", "endpoint string");
}

void TestParseRejectsInvalidText() {
  conn_util::Endpoint endpoint("1.2.3.4", 1234);

  Require(!conn_util::Endpoint::parse("", &endpoint), "reject empty text");
  Require(!conn_util::Endpoint::parse(":123", &endpoint), "reject empty host");
  Require(!conn_util::Endpoint::parse("127.0.0.1:", &endpoint),
          "reject empty port");
  Require(!conn_util::Endpoint::parse("127.0.0.1:0", &endpoint),
          "reject zero port");
  Require(!conn_util::Endpoint::parse("127.0.0.1:65536", &endpoint),
          "reject port over uint16");
  Require(!conn_util::Endpoint::parse("127.0.0.1:abc", &endpoint),
          "reject non-numeric port");
  Require(!conn_util::Endpoint::parse("127.0.0.1:12x", &endpoint),
          "reject trailing port bytes");
  Require(!conn_util::Endpoint::parse("127.0.0.1:1", nullptr),
          "reject null output pointer");

  RequireEqual(endpoint.toString(), "1.2.3.4:1234",
               "failed parse leaves existing endpoint unchanged");
}

void TestSockAddrConversion() {
  conn_util::Endpoint endpoint("127.0.0.1", 8080);
  sockaddr_in addr;
  Require(endpoint.toSockAddr(&addr), "convert loopback endpoint");
  RequireEqualInt(addr.sin_family, AF_INET, "sockaddr family");
  RequireEqualInt(ntohs(addr.sin_port), 8080, "sockaddr port");
  Require(addr.sin_addr.s_addr == htonl(INADDR_LOOPBACK),
          "sockaddr loopback address");

  conn_util::Endpoint any_star("*", 9000);
  Require(any_star.toSockAddr(&addr), "convert star wildcard endpoint");
  Require(addr.sin_addr.s_addr == htonl(INADDR_ANY), "star maps to any");

  conn_util::Endpoint any_zero("0", 9001);
  Require(any_zero.toSockAddr(&addr), "convert zero wildcard endpoint");
  Require(addr.sin_addr.s_addr == htonl(INADDR_ANY), "zero maps to any");

  conn_util::Endpoint any_addr("0.0.0.0", 9002);
  Require(any_addr.toSockAddr(&addr), "convert any IPv4 endpoint");
  Require(addr.sin_addr.s_addr == htonl(INADDR_ANY), "0.0.0.0 maps to any");

  conn_util::Endpoint invalid("not-an-ip", 9003);
  Require(!invalid.toSockAddr(&addr), "reject non-numeric IPv4 host");
  Require(!endpoint.toSockAddr(nullptr), "reject null sockaddr output pointer");
}

}  // namespace

int main() {
  try {
    TestStatusBasics();
    TestParseValidEndpoint();
    TestParseRejectsInvalidText();
    TestSockAddrConversion();
    std::cout << "endpoint_test passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
