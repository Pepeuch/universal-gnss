// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_ntrip_connection.hpp"
#include "ug_rtcm.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
static std::int64_t Now()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
int main()
{
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  socklen_t len = sizeof(address);
  assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &len) == 0);
  assert(listen(listener, 1) == 0);
  std::atomic<bool> done{false};
  std::vector<std::uint8_t> frame(26, 0);
  frame[0] = 0xd3;
  frame[2] = 20;
  std::uint32_t crc;
  assert(ug_rtcm_crc24q(frame.data(), 23, &crc));
  frame[23] = crc >> 16;
  frame[24] = crc >> 8;
  frame[25] = crc;
  std::thread server([&] {
    int client = accept(listener, nullptr, nullptr);
    assert(client >= 0);
    timeval timeout{2, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    char request[1024];
    const auto size = recv(client, request, sizeof(request), 0);
    assert(size > 0);
    assert(std::string(request, size).find("GET /MOUNT HTTP/1.1") != std::string::npos);
    const std::string header = "HTTP/1.1 200 OK\r\nContent-Type: gnss/data\r\n\r\n";
    std::vector<std::uint8_t> response(header.begin(), header.end());
    response.insert(response.end(), frame.begin(), frame.end());
    response.insert(response.end(), frame.begin(), frame.end());
    assert(send(client, response.data(), response.size(), 0) ==
           static_cast<ssize_t>(response.size()));
    const auto deadline = Now() + 2000000000LL;
    while (!done && Now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    close(client);
  });
  std::vector<std::vector<std::uint8_t>> received;
  std::vector<bool> boundaries;
  ug::embedded::NtripConnection connection([&](const auto& f) { received.push_back(f.raw_bytes); },
                                           [&](bool open) { boundaries.push_back(open); });
  universal_gnss_ntrip::NtripConfig config;
  config.host = "127.0.0.1";
  config.port = ntohs(address.sin_port);
  config.mountpoint = "MOUNT";
  auto invalid = config;
  invalid.mountpoint = "MOUNT\r\nInjected: yes";
  assert(!connection.Configure(invalid, true));
  assert(connection.Configure(config, true));
  const auto deadline = Now() + 1500000000LL;
  while (received.size() < 2 && Now() < deadline)
    connection.Tick(Now());
  assert(received.size() == 2 && received[0] == frame && received[1] == frame);
  assert(connection.client().IsCorrectionFlowing());
  assert(connection.client().metrics().rtcm_frames_received == 2);
  assert(boundaries.size() == 1 && boundaries[0]);
  done = true;
  server.join();
  connection.Tick(Now());
  assert(!connection.client().IsConnected());
  assert(boundaries.size() == 2 && !boundaries.back());
  assert(connection.client().reconnect_state().attempt_count > 0);
  connection.Stop();
  assert(!connection.enabled());
  close(listener);
}
