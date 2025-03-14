#include <fmt/core.h>
#include <gflags/gflags.h>
#include <liburing.h>
#include <netinet/ip.h>

#include <cassert>
#include <iostream>
#include <memory>
#include <unordered_map>

#include "tcp_sockets.hpp"

DEFINE_bool(polling, false, "use io_uring polling mode");
DEFINE_bool(batch_submit, true, "submit io_uring requests in batch");
DEFINE_int32(backlog, 100, "backlog");
DEFINE_uint32(port, 54322, "listening port");
DEFINE_uint32(sq_size, 1024, "io_uring submission queue size");
DEFINE_uint32(cq_size, 0, "io_uring completion queue size");
DEFINE_uint32(thread_idle, 5, "max idle time of submission queue polling thread, in seconds");
DEFINE_uint64(max_message_size, 1024, "maximum message size in bytes");

void Submit(io_uring *ring) {
  if (auto r = io_uring_submit(ring); r < 0) {
    throw std::runtime_error(fmt::format("submit failed: {}", strerror(-r)));
  } else if (r == 0) {
    throw std::runtime_error("no request submitted");
  }
}

class Context {
 public:
  virtual ~Context() = default;

  virtual bool OnComplete(io_uring *ring, io_uring_cqe *cqe) = 0;
};

class ConnectionContext final : public Context {
 public:
  explicit ConnectionContext(std::unique_ptr<tcp::Connection> conn) : conn_{std::move(conn)}, buffer_{}, type_{kRecv} {}

  void AddRecv(io_uring *ring, bool submit);

  void AddSend(io_uring *ring, bool submit);

  bool OnComplete(io_uring *ring, io_uring_cqe *cqe) override;

 private:
  enum RequestType { kRecv, kSend };

  bool OnRecvComplete(io_uring *ring, io_uring_cqe *cqe);
  bool OnSendComplete(io_uring *ring, io_uring_cqe *cqe);
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
    Submit(ring);
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
    Submit(ring);
  }

  type_ = kSend;
}

bool ConnectionContext::OnComplete(io_uring *ring, io_uring_cqe *cqe) try {
  if (type_ == kRecv) {
    return OnRecvComplete(ring, cqe);
  } else {
    return OnSendComplete(ring, cqe);
  }
} catch (const std::exception &e) {
  std::cerr << conn_->peer() << ": " << e.what() << '\n';
  delete this;
  return false;
}

bool ConnectionContext::OnRecvComplete(io_uring *ring, io_uring_cqe *cqe) {
  assert(type_ == kRecv);
  if (cqe->res < 0) {
    throw SocketException(-cqe->res);
  } else if (cqe->res == 0) {
    throw std::runtime_error("peer closed");
  } else {
    buffer_.resize(cqe->res);
    Write(&buffer_);
    if (buffer_.empty()) {
      AddRecv(ring, !FLAGS_batch_submit);
    } else {
      AddSend(ring, !FLAGS_batch_submit);
    }
    return FLAGS_batch_submit;
  }
}

bool ConnectionContext::OnSendComplete(io_uring *ring, io_uring_cqe *cqe) {
  assert(type_ == kSend);
  if (cqe->res < 0) {
    throw SocketException(-cqe->res);
  } else if (cqe->res < (int32_t)buffer_.size()) {
    buffer_.erase(buffer_.begin(), buffer_.begin() + cqe->res);
    AddSend(ring, !FLAGS_batch_submit);
  } else {
    assert(cqe->res == (int32_t)buffer_.size());
    AddRecv(ring, !FLAGS_batch_submit);
  }
  return FLAGS_batch_submit;
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

  void AddAccept(io_uring *ring, bool submit);

  bool OnComplete(io_uring *ring, io_uring_cqe *cqe) override;

  AcceptContext(const AcceptContext &) = delete;   // Delete copy ctor
  AcceptContext(AcceptContext &&) = delete;        // Delete move ctor: now usage now
  void operator=(const AcceptContext &) = delete;  // Delete copy assignment
  void operator=(AcceptContext &&) = delete;       // Delete move assignment: no usage now

 private:
  tcp::ServerSocket *server_;
};

void AcceptContext::AddAccept(io_uring *ring, bool submit) {
  auto sqe = io_uring_get_sqe(ring);
  if (sqe == nullptr) {
    throw std::runtime_error("get sqe failed");
  }
  io_uring_prep_multishot_accept(sqe, server_->fd(), nullptr, nullptr, 0);
  sqe->user_data = reinterpret_cast<uintptr_t>(this);
  if (submit) {
    Submit(ring);
  }
}

bool AcceptContext::OnComplete(io_uring *ring, io_uring_cqe *cqe) {
  if (cqe->res < 0) {
    throw AcceptException(-cqe->res);
  }

  auto conn = std::make_unique<tcp::Connection>(cqe->res);
  conn->SetNonBlocking();

  std::cout << conn->peer() << " connected\n";

  auto conn_ctx = new ConnectionContext(std::move(conn));
  conn_ctx->AddRecv(ring, !FLAGS_batch_submit);

  if (!(cqe->flags & IORING_CQE_F_MORE)) {
    AddAccept(ring, !FLAGS_batch_submit);
  }
  return FLAGS_batch_submit;
}

void HandleCompleteEvents(io_uring *ring) {
  const int kSize = 128;
  io_uring_cqe *cqes[kSize];
  while (true) {
    if (auto r = io_uring_wait_cqe(ring, &cqes[0]); r != 0) {
      throw std::runtime_error(fmt::format("wait cqe failed: {}", strerror(-r)));
    }

    auto submit = false;
    auto nready = io_uring_peek_batch_cqe(ring, cqes, kSize);
    for (auto i = 0u; i < nready; i++) {
      auto cb = static_cast<Context *>(io_uring_cqe_get_data(cqes[i]));
      submit |= cb->OnComplete(ring, cqes[i]);
    }

    if (submit) {
      Submit(ring);
    }

    io_uring_cq_advance(ring, nready);
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

  auto params = io_uring_params{};
  memset(&params, 0, sizeof(params));

  if (FLAGS_cq_size > 0) {
    params.cq_entries = FLAGS_cq_size;
    params.flags |= IORING_SETUP_CQSIZE;
  }
  if (FLAGS_polling) {
    params.sq_thread_idle = FLAGS_thread_idle * 1000;
    params.flags |= IORING_SETUP_SQPOLL;
  }

  auto ring = io_uring{};
  if (auto r = io_uring_queue_init_params(FLAGS_sq_size, &ring, &params); r < 0) {
    std::cerr << "init queue failed: %s" << strerror(-r) << '\n';
    return -1;
  }

  auto ctx = AcceptContext(&server);
  ctx.AddAccept(&ring, true);

  try {
    HandleCompleteEvents(&ring);
  } catch (std::exception &ex) {
    std::cerr << ex.what() << '\n';
  }

  io_uring_queue_exit(&ring);

  return 0;
}
