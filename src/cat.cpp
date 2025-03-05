#include <sys/fcntl.h>
#include <sys/stat.h>

#include <exception>
#include <fmt/core.h>

#include <liburing.h>
#include <cstdio>
#include <cstring>

const int kBufferSize = 1024 * 1024;
char gBuffer[kBufferSize];

auto GetFileSize(int fd) -> off_t {
    struct stat st {};
    if (auto r = ::fstat(fd, &st); r != 0) {
        throw std::runtime_error(strerror(errno));
    }
    if (S_ISREG(st.st_mode)) {
        return st.st_size;
    } else {
        throw std::runtime_error("not a regular file");
    }
}

// return the number of bytes read
auto Read(struct io_uring *ring, int fd, char *buffer, uint32_t count, uint64_t offset) -> int {
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;

    sqe = io_uring_get_sqe(ring);
    if (sqe == nullptr) {
        throw std::runtime_error("Get sqe failed");
    }

    io_uring_prep_read(sqe, fd, buffer, count, offset);

    if (auto r = io_uring_submit(ring); r < 0) {
        throw std::runtime_error(fmt::format("submit failed: {}", std::strerror(-r)));
    } else if (r < 1) {
        throw std::runtime_error(fmt::format("Submitted only {}", r));
    }

    if (auto r = io_uring_wait_cqe(ring, &cqe); r < 0) {
        throw std::runtime_error(fmt::format("wait cqe failed: {}", std::strerror(-r)));
    }

    if (cqe->res < 0) {
        throw std::runtime_error(fmt::format("read failed: {}", std::strerror(-cqe->res)));
    }

    int nr = cqe->res;

    io_uring_cqe_seen(ring, cqe);

    return nr;
}

auto Write(struct io_uring *ring, int fd, const char *buffer, uint32_t buffer_len) {
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;

    sqe = io_uring_get_sqe(ring);
    if (sqe == nullptr) {
        throw std::runtime_error("get sqe failed");
    }

    io_uring_prep_write(sqe, fd, buffer, buffer_len, -1);

    if (auto r = io_uring_submit(ring); r < 0) {
        throw std::runtime_error(fmt::format("submit failed: {}", std::strerror(-r)));
    } else if (r < 1) {
        throw std::runtime_error(fmt::format("Submitted only {}", r));
    }

    if (auto r = io_uring_wait_cqe(ring, &cqe); r < 0) {
        throw std::runtime_error(fmt::format("wait cqe failed: {}", std::strerror(-r)));
    }

    if (cqe->res < 0) {
        throw std::runtime_error(fmt::format("write failed: {}", std::strerror(-cqe->res)));
    }
    if (cqe->res != buffer_len) {
        throw std::runtime_error(fmt::format("Got {} write size, expect {}", cqe->res, buffer_len));
    }

    io_uring_cqe_seen(ring, cqe);
}

auto Close(struct io_uring *ring, int fd) {
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;

    sqe = io_uring_get_sqe(ring);
    if (sqe == nullptr) {
        throw std::runtime_error("get sqe failed");
    }

    io_uring_prep_close(sqe, fd);

    if (auto r = io_uring_submit(ring); r < 0) {
        throw std::runtime_error(fmt::format("submit failed: {}", std::strerror(-r)));
    } else if (r < 1) {
        throw std::runtime_error(fmt::format("Submitted only {}", r));
    }

    if (auto r = io_uring_wait_cqe(ring, &cqe); r < 0) {
        throw std::runtime_error(fmt::format("wait cqe failed: {}", std::strerror(-r)));
    }

    if (cqe->res < 0) {
        throw std::runtime_error(fmt::format("close failed: {}", std::strerror(-cqe->res)));
    }

    io_uring_cqe_seen(ring, cqe);
}

auto ReadAndPrintFile(struct io_uring *ring, int fd) {
    auto file_size = GetFileSize(fd);
    for (off_t offset = 0; offset < file_size; offset += kBufferSize) {
        auto length = std::min<unsigned>(kBufferSize, file_size - offset);
        auto nread = Read(ring, fd, gBuffer, length, offset);
        Write(ring, STDOUT_FILENO, gBuffer, nread);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s file ...\n", argv[0]);
        return -1;
    }

    struct io_uring ring {};
    if (int r = io_uring_queue_init(1, &ring, 0); r != 0) {
        std::fprintf(stderr, "init queue failed: %s", std::strerror(-r));
        return -1;
    }

    for (int i = 1; i < argc; i++) {
        int fd = ::open(argv[i], O_RDONLY);
        if (fd < 0) {
            perror("open");
            return -1;
        }
        ReadAndPrintFile(&ring, fd);
        Close(&ring, fd);
    }

    io_uring_queue_exit(&ring);
}