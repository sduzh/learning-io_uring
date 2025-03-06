#include <fmt/core.h>
#include <gflags/gflags.h>
#include <sys/epoll.h>

#include <cassert>
#include <cstdio>
#include <iostream>
#include <memory>
#include <vector>

#include "tcp_sockets.hpp"

DEFINE_int32(backlog, 100, "backlog");
DEFINE_uint32(port, 54322, "listening port");
DEFINE_uint64(max_message_size, 1024, "maximum message size in bytes");

class Context {
 public:
  virtual ~Context() = default;

  virtual void HandleEpollEvent(int epoll_fd, epoll_event* event) = 0;
};

class ServerContext final : public Context {
 public:
  explicit ServerContext(tcp::ServerSocket* server) : server_(server) {}

  void HandleEpollEvent(int epoll_fd, epoll_event* event) override;

 private:
  tcp::ServerSocket* server_;
};

class ConnectionContext final : public Context {
 public:
  explicit ConnectionContext(std::unique_ptr<tcp::Connection> conn) : conn_(std::move(conn)) {}

  void HandleEpollEvent(int epoll_fd, epoll_event* event) override;

 private:
  void DoHandleEpollEvent(int epoll_fd, epoll_event* event);
  void Write(std::string* message);

  std::unique_ptr<tcp::Connection> conn_;
  std::string buffer_;
};

void ServerContext::HandleEpollEvent(int epoll_fd, epoll_event* event) {
  assert(event->events == EPOLLIN);
  (void)event;
  auto conn = server_->Accept();
  conn->SetNonBlocking();
  std::cout << conn->peer() << " connected\n";

  auto conn_fd = conn->fd();
  auto conn_ctx = std::make_unique<ConnectionContext>(std::move(conn));
  auto new_ev = epoll_event{};
  new_ev.events = EPOLLIN /* | EPOLLET*/;
  new_ev.data.ptr = conn_ctx.release();
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &new_ev)) {
    perror("epoll_ctl");
    exit(EXIT_FAILURE);
  }
}

void ConnectionContext::HandleEpollEvent(int epoll_fd, epoll_event* event) {
  try {
    DoHandleEpollEvent(epoll_fd, event);
  } catch (const std::exception& ex) {
    std::cerr << conn_->peer() << ": " << ex.what() << '\n';
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn_->fd(), nullptr)) {
      perror("epoll_ctl");
      exit(EXIT_FAILURE);
    }
    delete this;
  }
}

void ConnectionContext::DoHandleEpollEvent(int epoll_fd, epoll_event* event) {
  if (event->events & EPOLLIN) {
    auto old_size = buffer_.size();
    buffer_.resize(old_size + FLAGS_max_message_size);
    auto nr = conn_->Read(&buffer_[old_size], FLAGS_max_message_size);
    if (nr == 0) {  // peer closed
      throw std::runtime_error("connection closed by peer");
    }
    buffer_.resize(old_size + nr);

    Write(&buffer_);

    if (!buffer_.empty()) {
      // Remove EPOLLOUT event
      auto new_ev = epoll_event{};
      new_ev.data.ptr = this;
      new_ev.events = EPOLLIN;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn_->fd(), &new_ev)) {
        throw std::runtime_error(fmt::format("epoll_ctl failed: {}", strerror(errno)));
      }
    }
  }

  if (event->events & EPOLLOUT) {
    assert(!buffer_.empty());

    Write(&buffer_);

    if (buffer_.empty()) {
      // Remove EPOLLOUT event
      auto new_ev = epoll_event{};
      new_ev.data.ptr = this;
      new_ev.events = EPOLLIN;
      if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn_->fd(), &new_ev)) {
        throw std::runtime_error(fmt::format("epoll_ctl failed: {}", strerror(errno)));
      }
    }
  }
}

void ConnectionContext::Write(std::string* message) {
  auto nw = (size_t)0;
  try {
    nw = conn_->Write(message->data(), message->size());
  } catch (const SocketBlockException& e) {
    // ignore
  }
  message->erase(message->begin(), message->begin() + (int64_t)nw);
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto server = tcp::ServerSocket();
  server.Bind(Endpoint(fmt::format("0.0.0.0:{}", FLAGS_port)));
  server.Listen(FLAGS_backlog);

  auto server_ctx = ServerContext(&server);

  int epoll_fd = epoll_create(1 /*ignored but must be greater than 0*/);
  if (epoll_fd == -1) {
    perror("epoll_create");
    return -1;
  }

  auto ev = epoll_event{};
  ev.events = EPOLLIN;
  ev.data.ptr = &server_ctx;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server.fd(), &ev)) {
    perror("epoll_ctl");
    return -1;
  }

  const int kMaxEvents = 100;
  epoll_event events[kMaxEvents];

  for (;;) {
    auto num_fds = epoll_wait(epoll_fd, events, kMaxEvents, -1);
    if (num_fds == -1) {
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }
    for (int i = 0; i < num_fds; i++) {
      auto& event = events[i];
      auto ctx = reinterpret_cast<Context*>(event.data.ptr);
      ctx->HandleEpollEvent(epoll_fd, &event);
    }
  }
}