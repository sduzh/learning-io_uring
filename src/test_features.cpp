#include <liburing.h>
#include <stdio.h>
#include <string.h>

int main(int /*argc*/, char** /*argv*/) {
  auto params = ::io_uring_params{};
  ::memset(&params, 0, sizeof(params));

  if (auto r = ::io_uring_setup(1, &params); r < 0) {
    fprintf(stderr, "%s\n", ::strerror(-r));
    return -1;
  }

#define TEST_FEATURE(FEATURE_NAME)                                      \
  do {                                                                  \
    bool support = (params.features & (FEATURE_NAME));                  \
    fprintf(stdout, "%s: %s\n", #FEATURE_NAME, support ? "Yes" : "No"); \
  } while (0)

#if defined(IORING_FEAT_SINGLE_MMAP)
  TEST_FEATURE(IORING_FEAT_SINGLE_MMAP);
#endif

#if defined(IORING_FEAT_NODROP)
  TEST_FEATURE(IORING_FEAT_NODROP);
#endif

#if defined(IORING_FEAT_SUBMIT_STABLE)
  TEST_FEATURE(IORING_FEAT_SUBMIT_STABLE);
#endif

#if defined(IORING_FEAT_RW_CUR_POS)
  TEST_FEATURE(IORING_FEAT_RW_CUR_POS);
#endif

#if defined(IORING_FEAT_CUR_PERSONALITY)
  TEST_FEATURE(IORING_FEAT_CUR_PERSONALITY);
#endif

#if defined(IORING_FEAT_FAST_POLL)
  TEST_FEATURE(IORING_FEAT_FAST_POLL);
#endif

#if defined(IORING_FEAT_POLL_32BITS)
  TEST_FEATURE(IORING_FEAT_POLL_32BITS);
#endif

#if defined(IORING_FEAT_SQPOLL_NONFIXED)
  TEST_FEATURE(IORING_FEAT_SQPOLL_NONFIXED);
#endif

#if defined(IORING_FEAT_ENTER_EXT_ARG)
  TEST_FEATURE(IORING_FEAT_ENTER_EXT_ARG);
#endif

#if defined(IORING_FEAT_NATIVE_WORKERS)
  TEST_FEATURE(IORING_FEAT_NATIVE_WORKERS);
#endif

#if defined(IORING_FEAT_RSRC_TAGS)
  TEST_FEATURE(IORING_FEAT_RSRC_TAGS);
#endif

  return 0;
}