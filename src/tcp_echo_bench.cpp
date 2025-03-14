#include <gflags/gflags.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "tcp_sockets.hpp"

DEFINE_string(server, "127.0.0.1:54322", "address of echo server");
DEFINE_int32(message_size, 64, "test message length");
DEFINE_int32(duration, 60, "test duration in seconds");
DEFINE_int32(connections, 1, "number of clients");
DEFINE_bool(streaming, false, "streaming mode");

struct Stats {
  std::atomic<uint64_t> stopped{false};
  std::atomic<uint64_t> sent{0};
  std::atomic<uint64_t> received{0};
};

void SendAndReceive(tcp::ClientSocket& client, Stats& stats, std::atomic<bool>& started, std::atomic<bool>& stopped) try {
  std::vector<char> buffer(FLAGS_message_size, 'X');
  while (!started.load(std::memory_order_acquire)) {
  }

  while (!stopped.load(std::memory_order_acquire)) {
    client.WriteAll(buffer.data(), buffer.size());
    client.ReadAll(buffer.data(), buffer.size());
    stats.received.fetch_add(1, std::memory_order_release);
  }
  if (auto n = std::count(buffer.begin(), buffer.end(), 'X'); n != FLAGS_message_size) {
    throw std::runtime_error("unexpected buffer content");
  }
} catch (const std::exception& ex) {
  std::cerr << ex.what() << '\n';
  throw;
}

void Send(tcp::ClientSocket& client, Stats& stats, std::atomic<bool>& started, std::atomic<bool>& stopped) try {
  std::vector<char> buffer(FLAGS_message_size, 'X');
  while (!started.load(std::memory_order_acquire)) {
  }

  while (!stopped.load(std::memory_order_acquire)) {
    client.WriteAll(buffer.data(), buffer.size());
    stats.sent.fetch_add(1, std::memory_order_release);
  }
  stats.stopped.store(1, std::memory_order_release);
} catch (const std::exception& ex) {
  std::cerr << ex.what() << '\n';
  throw;
}

void Receive(tcp::ClientSocket& client, Stats& stats) try {
  std::vector<char> buffer(FLAGS_message_size, 'X');

  while (!stats.stopped.load(std::memory_order_acquire) ||
         stats.received.load(std::memory_order_acquire) < stats.sent.load(std::memory_order_acquire)) {
    client.ReadAll(buffer.data(), buffer.size());
    stats.received.fetch_add(1, std::memory_order_release);
  }
  if (auto n = std::count(buffer.begin(), buffer.end(), 'X'); n != FLAGS_message_size) {
    throw std::runtime_error("unexpected buffer content");
  }
} catch (const std::exception& ex) {
  std::cerr << ex.what() << '\n';
  throw;
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  auto stopped = std::atomic<bool>(false);
  auto started = std::atomic<bool>(false);
  auto peer = Endpoint(FLAGS_server);
  auto stats = std::vector<Stats>(FLAGS_connections);
  auto clients = std::vector<tcp::ClientSocket>(FLAGS_connections);
  for (auto& client : clients) {
    client.Connect(peer);
  }
  auto num_threads = FLAGS_streaming ? 2 * FLAGS_connections : FLAGS_connections;
  auto threads = std::vector<std::thread>();
  threads.reserve(num_threads);

  if (!FLAGS_streaming) {
    for (int i = 0; i < FLAGS_connections; i++) {
      auto thread = std::thread([&, i]() {
        auto& stat = stats[i];
        auto& client = clients[i];
        SendAndReceive(client, stat, started, stopped);
      });
      threads.emplace_back(std::move(thread));
    }
  } else {
    // Create receive threads
    for (int i = 0; i < FLAGS_connections; i++) {
      auto thread = std::thread([&, i]() {
        auto& stat = stats[i];
        auto& client = clients[i];
        Receive(client, stat);
      });
      threads.emplace_back(std::move(thread));
    }

    // Create send threads
    for (int i = 0; i < FLAGS_connections; i++) {
      auto thread = std::thread([&, i]() {
        auto& stat = stats[i];
        auto& client = clients[i];
        Send(client, stat, started, stopped);
      });
      threads.emplace_back(std::move(thread));
    }
  }

  started.store(true, std::memory_order_release);

  auto last_received = (uint64_t)0;
  for (int i = 0; i < FLAGS_duration; i++) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    auto received = (uint64_t)0;
    for (auto& st : stats) {
      received += st.received.load(std::memory_order_acquire);
    }

    auto qps = (received - last_received);
    auto throughput = qps * 2 * FLAGS_message_size / 1024 / 1024;
    std::cout << fmt::format("qps: {} throughput: {} (MiB/s)\n", qps, throughput);
    last_received = received;
  }

  stopped.store(true, std::memory_order_release);

  for (auto& t : threads) {
    t.join();
  }

  return 0;
}