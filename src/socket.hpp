#pragma once

#include <fcntl.h>
#include <unistd.h>  // close

#include <exception>

#include "endpoint.hpp"

class SocketException : public std::runtime_error {
 public:
  explicit SocketException(int err) : std::runtime_error(fmt::format("E{}: {}", err, ::strerror(err))), errno_(err) {}

  int GetErrno() const { return errno_; }

 private:
  int errno_;
};

class SocketBlockException final : public SocketException {
 public:
  explicit SocketBlockException(int err) : SocketException(err) {}
};

class ConnectException final : public SocketException {
 public:
  explicit ConnectException(int err) : SocketException(err) {}
};

class BindException final : public SocketException {
 public:
  explicit BindException(int err) : SocketException(err) {}
};

class ListenException final : public SocketException {
 public:
  explicit ListenException(int err) : SocketException(err) {}
};

class AcceptException final : public SocketException {
 public:
  explicit AcceptException(int err) : SocketException(err) {}
};

class Socket {
 public:
  Socket() : fd_(-1) {}

  explicit Socket(int fd) : fd_(fd) {
    if (fd_ < 0) throw std::invalid_argument("fd is negative");
  }

  virtual ~Socket() { Close(); }

  // Disallow copy and assign
  Socket(const Socket&) = delete;
  void operator=(const Socket&) = delete;

  // Move ctor and assignment
  Socket(Socket&&) noexcept;
  Socket& operator=(Socket&&) noexcept;

  void Close();

  [[nodiscard]] auto Write(const char* buf, size_t length) -> size_t { return Write(fd_, buf, length); }

  void WriteAll(const char* buf, size_t length) { WriteAll(fd_, buf, length); }

  [[nodiscard]] auto Read(char* buf, size_t length) -> size_t { return Read(fd_, buf, length); }

  void ReadAll(char* buf, size_t length) { ReadAll(fd_, buf, length); }

  void SetNonBlocking() { SetNonBlocking(fd_); }

  void SetReuseAddr() { SetReuseAddr(fd_); }

  [[nodiscard]] auto fd() const -> int { return fd_; }

  [[nodiscard]] auto ReleaseFd() -> int;

  void Swap(Socket& rhs) noexcept { std::swap(fd_, rhs.fd_); }

  static void SetNonBlocking(int fd);

  static void SetReuseAddr(int fd);

  static auto Read(int fd, char* buf, size_t length) -> size_t;

  static void ReadAll(int fd, char* buf, size_t length);

  [[nodiscard]] static auto Write(int fd, const char* buf, size_t length) -> size_t;

  static void WriteAll(int fd, const char* buffer, size_t length);

  static auto GetLocalEndpoint(int fd) -> Endpoint;

  static auto GetPeerEndpoint(int fd) -> Endpoint;

 protected:
  int fd_;
};

// Move ctor
inline Socket::Socket(Socket&& rhs) noexcept : Socket(rhs.fd_) { rhs.fd_ = -1; }

// Move assignment
inline Socket& Socket::operator=(Socket&& rhs) noexcept {
  this->~Socket();
  fd_ = rhs.fd_;
  rhs.fd_ = -1;
  return *this;
}

inline void Socket::Close() {
  if (fd_ >= 0) {
    (void)::close(fd_);
    fd_ = -1;
  }
}

inline auto Socket::GetPeerEndpoint(int fd) -> Endpoint {
  auto peer_addr = sockaddr_in{};
  auto len = static_cast<socklen_t>(sizeof(peer_addr));
  if (::getpeername(fd, reinterpret_cast<sockaddr*>(&peer_addr), &len) != 0) {
    throw std::runtime_error(fmt::format("getpeername failed: E{}: {}", errno, strerror(errno)));
  }
  return Endpoint(peer_addr);
}

inline auto Socket::GetLocalEndpoint(int fd) -> Endpoint {
  auto local_addr = sockaddr_in{};
  auto len = static_cast<socklen_t>(sizeof(local_addr));
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&local_addr), &len) != 0) {
    throw std::runtime_error(fmt::format("getsockname failed: E{}: {}", errno, strerror(errno)));
  }
  return Endpoint(local_addr);
}

inline auto Socket::Read(int fd, char* buf, size_t length) -> size_t {
  while (true) {
    auto r = ::recv(fd, buf, length, 0);
    if (r == -1) {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        throw SocketBlockException(errno);
      } else {
        throw SocketException(errno);
      }
    } else {
      return r;
    }
  }
}

inline void Socket::ReadAll(int fd, char* buf, size_t length) {
  auto off = (size_t)0;
  do {
    off += Read(fd, buf + off, length - off);
  } while (off < length);
}

inline void swap(Socket& lhs, Socket& rhs) { lhs.Swap(rhs); }

void Socket::SetNonBlocking(int fd) {
  if (fd < 0) {
    throw std::invalid_argument(fmt::format("invalid fd: {}", fd));
  }
  auto flags = ::fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    throw std::runtime_error(fmt::format("fcntl failed for fd {}: {}", fd, ::strerror(errno)));
  }
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags)) {
    throw std::runtime_error(fmt::format("fcntl failed for fd {}: {}", fd, ::strerror(errno)));
  }
}

void Socket::SetReuseAddr(int fd) {
  const int enabled = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(int))) {
    throw std::runtime_error(fmt::format("setsockopt failed: {}", ::strerror(errno)));
  }
}

inline auto Socket::Write(int fd, const char* buf, size_t length) -> size_t {
  while (true) {
    auto r = ::send(fd, buf, length, 0);
    if (r == -1) {
      if (errno == EINTR) {
        continue;
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        throw SocketBlockException(errno);
      } else {
        throw SocketException(errno);
      }
    } else {
      return r;
    }
  }
}

inline void Socket::WriteAll(int fd, const char* buffer, size_t length) {
  auto off = (size_t)0;
  do {
    off += Write(fd, buffer + off, length - off);
  } while (off < length);
}

auto Socket::ReleaseFd() -> int {
  auto r = fd_;
  fd_ = -1;
  return r;
}