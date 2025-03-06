#pragma once

#include <liburing.h>
#include <netinet/ip.h>
#include <string.h>
#include <sys/socket.h>

namespace uring {
class Params {
 public:
  Params();

  auto CompletionQueueSize(uint32_t size) -> Params &;

  auto PollSubmissionQueue(uint32_t thread_idle_ms) -> Params &;

  // Bound the submission queue poll thread to the |cpu|.
  // Only meaningful when |PollSubmissionQueue| is set.
  auto BoundPollThreadToCPU(uint32_t cpu) -> Params &;

  auto ClampEntries() -> Params &;

  auto params() -> ::io_uring_params & { return params_; }

 private:
  ::io_uring_params params_;
};

inline Params::Params() { ::memset(&params_, 0, sizeof(params_)); }

inline auto Params::CompletionQueueSize(uint32_t size) -> Params & {
  params_.cq_entries = size;
  params_.flags |= IORING_SETUP_CQSIZE;
  return *this;
}

inline auto Params::PollSubmissionQueue(uint32_t thread_idle_ms) -> Params & {
  params_.sq_thread_idle = thread_idle_ms;
  params_.flags |= IORING_SETUP_SQPOLL;
  return *this;
}

inline auto Params::BoundPollThreadToCPU(uint32_t cpu) -> Params & {
  params_.sq_thread_cpu = cpu;
  params_.flags |= IORING_SETUP_SQ_AFF;
  return *this;
}

inline auto Params::ClampEntries() -> Params & {
  params_.flags |= IORING_SETUP_CLAMP;
  return *this;
}

}  // namespace uring