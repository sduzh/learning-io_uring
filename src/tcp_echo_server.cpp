#include <fmt/core.h>
#include <gflags/gflags.h>
#include <liburing.h>
#include <netinet/ip.h>

#include <cassert>
#include <iostream>
#include <memory>
#include <unordered_map>

#include "tcp_sockets.hpp"

DEFINE_int32(backlog, 100, "backlog");
DEFINE_uint32(port, 54322, "listening port");
DEFINE_uint64(max_message_size, 1024, "maximum message size in bytes");
DEFINE_uint32(max_connections, 1024, "maximum connections");

class Context {
 public:
  virtual ~Context() = default;

  virtual void OnComplete(io_uring *ring, io_uring_cqe *cqe) = 0;
};

class ConnectionContext final : public Context {
 public:
  explicit ConnectionContext(std::unique_ptr<tcp::Connection> conn) : conn_{std::move(conn)}, buffer_{}, type_{kRecv} {}

  void AddRecv(io_uring *ring, bool submit);

  void AddSend(io_uring *ring, bool submit);

  void OnComplete(io_uring *ring, io_uring_cqe *cqe) override;

 private:
  enum RequestType { kRecv, kSend };

  void OnRecvComplete(io_uring *ring, io_uring_cqe *cqe);
  void OnSendComplete(io_uring *ring, io_uring_cqe *cqe);
  void Write(std::string *message);

  std::unique_ptr<tcp::Connection> conn_;
  std::string buffer_;
  RequestType type_;
};

void ConnectionContext::AddRecv(io_uring *ring, bool submit) {
  buffer_.resize(FLAGS_max_message_size);
  auto sqe = io_uring_get_sqe(ring);
  if (sqe == nullptr) {
    throw std::runtime_error("get sqe failed");
  }
  io_uring_prep_recv(sqe, conn_->fd(), buffer_.data(), buffer_.size(), 0);
  sqe->user_data = reinterpret_cast<uintptr_t>(this);

  if (submit) {
    if (auto r = io_uring_submit(ring); r < 0) {
      throw std::runtime_error(fmt::format("submit failed: {}", strerror(-r)));
    } else if (r == 0) {
      throw std::runtime_error("submit sqe returned 0");
    }
  }

  type_ = kRecv;
}

void ConnectionContext::AddSend(io_uring *ring, bool submit) {
  auto sqe = io_uring_get_sqe(ring);
  if (sqe == nullptr) {
    throw std::runtime_error("get sqe failed");
  }
  io_uring_prep_send(sqe, conn_->fd(), buffer_.data(), buffer_.size(), 0);
  sqe->user_data = reinterpret_cast<uintptr_t>(this);

  if (submit) {
    if (auto r = io_uring_submit(ring); r < 0) {
      throw std::runtime_error(fmt::format("submit failed: {}", strerror(-r)));
    } else if (r == 0) {
      throw std::runtime_error("submitted 0 request");
    }
  }

  type_ = kSend;
}

void ConnectionContext::OnComplete(io_uring *ring, io_uring_cqe *cqe) try {
  if (type_ == kRecv) {
    OnRecvComplete(ring, cqe);
  } else {
    OnSendComplete(ring, cqe);
  }
} catch (const std::exception &e) {
  std::cerr << conn_->peer() << ": " << e.what() << '\n';
  delete this;
}

void ConnectionContext::OnRecvComplete(io_uring *ring, io_uring_cqe *cqe) {
  assert(type_ == kRecv);
  if (cqe->res < 0) {
    throw SocketException(-cqe->res);
  } else if (cqe->res == 0) {
    throw std::runtime_error("peer closed");
  } else {
    buffer_.resize(cqe->res);
    Write(&buffer_);
    if (buffer_.empty()) {
      AddRecv(ring, true);
    } else {
      AddSend(ring, true);
    }
  }
}

void ConnectionContext::OnSendComplete(io_uring *ring, io_uring_cqe *cqe) {
  assert(type_ == kSend);
  if (cqe->res < 0) {
    throw SocketException(-cqe->res);
  } else if (cqe->res < (int32_t)buffer_.size()) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + cqe->res);
    AddSend(ring, true);
  } else {
    assert(cqe->res == (int32_t)buffer_.size());
    AddRecv(ring, true);
  }
}

void ConnectionContext::Write(std::string *message) {
  size_t nw = 0;
  try {
    nw = conn_->Write(message->data(), message->size());
  } catch (const SocketBlockException & /*e*/) {
    // ignore
  }
  message->erase(message->begin(), message->begin() + (int64_t)nw);
}

class AcceptContext final : public Context {
 public:
  explicit AcceptContext(tcp::ServerSocket *server) : server_(server) {}

  ~AcceptContext() override = default;

  void AddAccept(io_uring *ring);

  void OnComplete(io_uring *ring, io_uring_cqe *cqe) override;

  AcceptContext(const AcceptContext &) = delete;   // Delete copy ctor
  AcceptContext(AcceptContext &&) = delete;        // Delete move ctor: now usage now
  void operator=(const AcceptContext &) = delete;  // Delete copy assignment
  void operator=(AcceptContext &&) = delete;       // Delete move assignment: no usage now

 private:
  tcp::ServerSocket *server_;
};

void AcceptContext::AddAccept(io_uring *ring) {
  auto sqe = io_uring_get_sqe(ring);
  if (sqe == nullptr) {
    throw std::runtime_error("get sqe failed");
  }
  io_uring_prep_multishot_accept(sqe, server_->fd(), nullptr, nullptr, 0);
  sqe->user_data = reinterpret_cast<uintptr_t>(this);
  if (auto r = io_uring_submit(ring); r < 0) {
    throw std::runtime_error(fmt::format("submit failed: {}", strerror(-r)));
  } else if (r == 0) {
    throw std::runtime_error("submit returned 0");
  }
}

void AcceptContext::OnComplete(io_uring *ring, io_uring_cqe *cqe) {
  if (cqe->res < 0) {
    throw AcceptException(-cqe->res);
  }

  auto conn = std::make_unique<tcp::Connection>(cqe->res);
  conn->SetNonBlocking();

  std::cout << conn->peer() << " connected\n";

  auto conn_ctx = new ConnectionContext(std::move(conn));
  conn_ctx->AddRecv(ring, true);

  if (!(cqe->flags & IORING_CQE_F_MORE)) {
    AddAccept(ring);
  }
}

void HandleCompleteEvents(io_uring *ring) {
  while (true) {
    io_uring_cqe *cqe;
    if (auto r = io_uring_wait_cqe(ring, &cqe); r != 0) {
      throw std::runtime_error(fmt::format("wait cqe failed: {}", strerror(-r)));
    }

    auto cb = reinterpret_cast<Context *>(cqe->user_data);
    cb->OnComplete(ring, cqe);

    io_uring_cqe_seen(ring, cqe);
  }
}

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto bind_addr = Endpoint(fmt::format("0.0.0.0:{}", FLAGS_port));
  auto server = tcp::ServerSocket();
  server.SetReuseAddr();
  // server.SetNonBlocking();
  server.Bind(bind_addr);
  server.Listen(FLAGS_backlog);

  std::cout << "Listening on " << bind_addr << '\n';

  auto ring = io_uring{};
  if (auto r = ::io_uring_queue_init(FLAGS_max_connections, &ring, 0); r < 0) {
    std::cerr << "init queue failed: %s" << strerror(-r) << '\n';
    return -1;
  }

  auto ctx = AcceptContext(&server);
  ctx.AddAccept(&ring);

  try {
    HandleCompleteEvents(&ring);
  } catch (std::exception &ex) {
    std::cerr << ex.what() << '\n';
  }

  io_uring_queue_exit(&ring);

  return 0;
}
