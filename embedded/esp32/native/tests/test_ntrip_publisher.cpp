// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_ntrip_publisher.hpp"
#include "ug_rtcm.h"
#include <cassert>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
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
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
  socklen_t len = sizeof(addr);
  assert(getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
  assert(listen(listener, 1) == 0);
  ug::embedded::Gateway::Frame frame;
  frame.size = 26;
  frame.bytes[0] = 0xd3;
  frame.bytes[2] = 20;
  std::uint32_t crc;
  assert(ug_rtcm_crc24q(frame.bytes.data(), 23, &crc));
  frame.bytes[23] = crc >> 16;
  frame.bytes[24] = crc >> 8;
  frame.bytes[25] = crc;
  std::thread server([&] {
    int fd = accept(listener, nullptr, nullptr);
    assert(fd >= 0);
    timeval tv{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    std::string request;
    char data[512];
    while (request.find("\r\n\r\n") == std::string::npos)
    {
      auto n = recv(fd, data, sizeof(data), 0);
      assert(n > 0);
      request.append(data, n);
    }
    assert(request.find("SOURCE test-password /MOUNT\r\n") == 0);
    assert(send(fd, "ICY 200 OK\r\n", 12, 0) == 12);
    std::vector<std::uint8_t> bytes;
    while (bytes.size() < frame.size * 2)
    {
      auto n = recv(fd, data, sizeof(data), 0);
      assert(n > 0);
      bytes.insert(bytes.end(), data, data + n);
    }
    assert(bytes.size() == frame.size * 2);
    for (unsigned i = 0; i < 2; ++i)
      assert(std::equal(
          frame.bytes.begin(), frame.bytes.begin() + frame.size, bytes.begin() + i * frame.size));
    close(fd);
  });
  ug::embedded::NtripPublisher publisher;
  universal_gnss_ntrip::NtripConfig config;
  config.host = "127.0.0.1";
  config.port = ntohs(addr.sin_port);
  config.mountpoint = "MOUNT";
  config.password = "test-password";
  assert(publisher.Configure(config, true));
  const auto deadline = Now() + 2000000000LL;
  while (!publisher.streaming() && Now() < deadline)
    publisher.Tick(Now());
  assert(publisher.streaming());
  frame.receipt_ns = Now() - 4000000000LL;
  assert(!publisher.Send(frame, Now()));
  frame.receipt_ns = Now();
  assert(publisher.Send(frame, Now()));
  assert(publisher.Send(frame, Now()));
  server.join();
  publisher.Tick(Now());
  assert(!publisher.streaming());
  assert(publisher.metrics().frames == 2 && publisher.metrics().expired == 1);
  publisher.Stop();
  auto invalid = config;
  invalid.password = "bad\r\nheader";
  assert(!publisher.Configure(invalid, true));
  bool cancelled = true;
  assert(publisher.Configure(config, true, [&] { return cancelled; }));
  publisher.Tick(Now());
  assert(!publisher.streaming());
  close(listener);
}
