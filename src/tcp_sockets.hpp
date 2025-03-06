#pragma once

#include <memory>

#include "socket.hpp"

namespace tcp {
//
//===---------------- ClientSocket ----------------===
//

class ClientSocket final : public Socket {
 public:
  ClientSocket() : Socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {}

  ~ClientSocket() override = default;

  // Move ctor and assignment
  ClientSocket(ClientSocket&&) noexcept = default;
  ClientSocket& operator=(ClientSocket&&) noexcept = default;

  void Connect(const Endpoint& peer);

  auto peer() const -> const Endpoint& { return peer_; }

 private:
  Endpoint peer_;
};

inline void ClientSocket::Connect(const Endpoint& peer) {
  auto& addr = peer.sockaddr();
  auto len = sizeof(addr);
  if (::connect(fd_, reinterpret_cast<const sockaddr*>(&addr), len) == -1) {
    throw ConnectException(errno);
  }
  peer_ = GetPeerEndpoint(fd_);
}
static_assert(not std::is_copy_constructible_v<ClientSocket>);
static_assert(not std::is_copy_assignable_v<ClientSocket>);
static_assert(std::is_move_constructible_v<ClientSocket>);
static_assert(std::is_move_assignable_v<ClientSocket>);

//
//===---------------- Connection ----------------===
//
class Connection final : public Socket {
 public:
  explicit Connection(int fd) : Socket(fd), peer_(GetPeerEndpoint(fd_)) {}

  // Move ctor and assignment
  Connection(Connection&&) noexcept = default;
  Connection& operator=(Connection&&) noexcept = default;

  auto peer() const -> const Endpoint& { return peer_; }

 private:
  Endpoint peer_;
};
static_assert(not std::is_copy_constructible_v<Connection>);
static_assert(not std::is_copy_assignable_v<Connection>);
static_assert(std::is_move_constructible_v<Connection>);
static_assert(std::is_move_assignable_v<Connection>);

//
//===---------------- ServerSocket ----------------===
//

class ServerSocket final : public Socket {
 public:
  ServerSocket() : Socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {}

  explicit ServerSocket(int fd) : Socket(fd) {}

  ~ServerSocket() override = default;

  void Bind(const Endpoint& bind_addr);

  void Listen(int backlog);

  auto Accept() -> std::unique_ptr<Connection>;
};

void ServerSocket::Bind(const Endpoint& bind_addr) {
  auto& addr = bind_addr.sockaddr();
  if (::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr))) {
    throw BindException(errno);
  }
}

void ServerSocket::Listen(int backlog) {
  if (::listen(fd_, backlog)) {
    throw ListenException(errno);
  }
}

auto ServerSocket::Accept() -> std::unique_ptr<Connection> {
  if (auto fd = ::accept(fd_, nullptr, nullptr); fd >= 0) {
    return std::make_unique<Connection>(fd);
  } else {
    throw AcceptException(errno);
  }
}

}  // namespace tcp