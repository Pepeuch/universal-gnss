// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_ntrip_publisher.hpp"
#include "ug_rtcm.h"
#include <array>
#include <cassert>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace ug::embedded;
static std::int64_t Now()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
int main()
{
  std::array<int, 2> listeners{};
  std::array<unsigned short, 2> ports{};
  std::array<std::vector<unsigned>, 2> received;
  std::array<std::thread, 2> servers;
  for (unsigned i = 0; i < 2; ++i)
  {
    listeners[i] = socket(AF_INET, SOCK_STREAM, 0);
    assert(listeners[i] >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listeners[i], reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    socklen_t size = sizeof(addr);
    assert(getsockname(listeners[i], reinterpret_cast<sockaddr*>(&addr), &size) == 0);
    ports[i] = ntohs(addr.sin_port);
    assert(listen(listeners[i], 1) == 0);
    servers[i] = std::thread([&, i] {
      int fd = accept(listeners[i], nullptr, nullptr);
      assert(fd >= 0);
      timeval timeout{5, 0};
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      std::string request;
      char c;
      while (request.find("\r\n\r\n") == std::string::npos)
      {
        assert(recv(fd, &c, 1, 0) == 1);
        request += c;
      }
      assert(request.find("SOURCE test /MOUNT\r\n") == 0);
      assert(send(fd, "ICY 200 OK\r\n", 12, 0) == 12);
      std::vector<unsigned char> bytes;
      unsigned char buffer[128];
      for (;;)
      {
        auto n = recv(fd, buffer, sizeof(buffer), 0);
        assert(n >= 0);
        if (!n)
          break;
        bytes.insert(bytes.end(), buffer, buffer + n);
      }
      assert(bytes.size() % 8 == 0);
      for (unsigned j = 0; j < bytes.size(); j += 8)
      {
        assert(ug_rtcm_frame_valid(bytes.data() + j, 8));
        received[i].push_back((unsigned(bytes[j + 3]) << 4) | (bytes[j + 4] >> 4));
      }
      close(fd);
    });
  }
  std::array<NtripPublisher, 2> publishers;
  for (unsigned i = 0; i < 2; ++i)
  {
    universal_gnss_ntrip::NtripConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = ports[i];
    cfg.mountpoint = "MOUNT";
    cfg.password = "test";
    assert(publishers[i].Configure(cfg, true, {}, i == 0 ? "1005(10),1077" : "1005(20),1087"));
  }
  auto deadline = Now() + 2000000000LL;
  while ((!publishers[0].streaming() || !publishers[1].streaming()) && Now() < deadline)
    for (auto& p : publishers)
      if (!p.streaming())
        p.Tick(Now());
  assert(publishers[0].streaming() && publishers[1].streaming());
  const auto base = Now();
  for (auto second : {0, 5, 10, 20})
    for (auto type : {1005, 1077, 1087, 1230})
    {
      Gateway::Frame frame;
      frame.size = 8;
      frame.bytes[0] = 0xd3;
      frame.bytes[2] = 2;
      frame.bytes[3] = type >> 4;
      frame.bytes[4] = (type & 15) << 4;
      std::uint32_t crc;
      assert(ug_rtcm_crc24q(frame.bytes.data(), 5, &crc));
      frame.bytes[5] = crc >> 16;
      frame.bytes[6] = crc >> 8;
      frame.bytes[7] = crc;
      frame.receipt_ns = base + second * 1000000000LL;
      for (auto& p : publishers)
        p.Send(frame, frame.receipt_ns);
    }
  for (auto& p : publishers)
    p.Stop();
  for (unsigned i = 0; i < 2; ++i)
  {
    servers[i].join();
    close(listeners[i]);
  }
  assert((received[0] == std::vector<unsigned>{1005, 1077, 1077, 1005, 1077, 1005, 1077}));
  assert((received[1] == std::vector<unsigned>{1005, 1087, 1087, 1087, 1005, 1087}));
  assert(publishers[0].metrics().frames == 7 && publishers[0].metrics().filtered == 9);
  assert(publishers[1].metrics().frames == 6 && publishers[1].metrics().filtered == 10);
}
