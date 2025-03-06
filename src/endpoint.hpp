#pragma once

#include <arpa/inet.h>
#include <fmt/core.h>
#include <netinet/ip.h>

#include <exception>
#include <iostream>

void ParseEndpoint(std::string_view endpoint, sockaddr_in* sockaddr);

class Endpoint {
 public:
  Endpoint() = default;

  // addr format: <ip>:<port>
  // example addr:
  //  0.0.0.0:80
  //  192.168.10.10:98080
  explicit Endpoint(std::string_view addr);

  explicit Endpoint(std::string_view host, uint16_t port);

  explicit Endpoint(sockaddr_in addr);

  auto host() const -> std::string;

  auto port() const -> uint16_t;

  auto sockaddr() const -> const ::sockaddr_in& { return sockaddr_; }

  auto to_string() const -> std::string;

 private:
  sockaddr_in sockaddr_{};
};

inline Endpoint::Endpoint(std::string_view addr) { ParseEndpoint(addr, &sockaddr_); }

inline Endpoint::Endpoint(std::string_view host, uint16_t port) { ParseEndpoint(fmt::format("{}:{}", host, port), &sockaddr_); }

inline Endpoint::Endpoint(sockaddr_in addr) : sockaddr_(addr) {}

inline auto Endpoint::host() const -> std::string { return {::inet_ntoa(sockaddr_.sin_addr)}; }

inline auto Endpoint::port() const -> uint16_t { return ::ntohs(sockaddr_.sin_port); }

inline auto Endpoint::to_string() const -> std::string { return fmt::format("{}:{}", host(), port()); }

inline std::ostream& operator<<(std::ostream& os, const Endpoint& endpoint) { return os << endpoint.host() << ":" << endpoint.port(); }

inline void ParseEndpoint(std::string_view endpoint, sockaddr_in* sockaddr) {
  auto buff = std::string(endpoint);
  auto sep = buff.find(':');
  if (sep == std::string::npos) {
    throw std::invalid_argument(fmt::format("Invalid address: {}", endpoint));
  }
  buff[sep] = '\0';
  if (::inet_aton(&buff[0], &sockaddr->sin_addr) == 0) {
    throw std::invalid_argument(fmt::format("Invalid address: {}", endpoint));
  }
  char* endptr = nullptr;
  auto port = strtol(&buff[sep + 1], &endptr, 10);
  if ((endptr == &buff[sep + 1]) || (*endptr != '\0') || (port < 0) || (port > UINT16_MAX)) {
    throw std::invalid_argument(fmt::format("Invalid address: {}", endpoint));
  }
  sockaddr->sin_port = ::htons(static_cast<uint16_t>(port));
  sockaddr->sin_family = AF_INET;
}

template <>
class fmt::formatter<Endpoint> {
 public:
  constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }
  template <typename Context>
  constexpr auto format(Endpoint const& e, Context& ctx) const {
    return format_to(ctx.out(), "{}:{}", e.host(), e.port());
  }
};
