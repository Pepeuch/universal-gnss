// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_ntrip_publisher.hpp"
#include "ug_rtcm.h"
#include <atomic>
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
  struct Trace
  {
    ug::embedded::NtripTraceKind kind;
    ug::embedded::NtripTraceError error;
  };
  std::vector<Trace> events;
  publisher.SetTrace({[](auto kind, auto error, auto code, void* context) {
                        assert(code == 0);
                        static_cast<std::vector<Trace>*>(context)->push_back({kind, error});
                      },
                      &events});
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
  using K = ug::embedded::NtripTraceKind;
  using E = ug::embedded::NtripTraceError;
  const std::vector<K> expected{
      K::kEnabled, K::kConnectAttempt, K::kConnected, K::kRequestSent, K::kAccepted};
  assert(events.size() == expected.size());
  for (std::size_t i = 0; i < events.size(); ++i)
    assert(events[i].kind == expected[i] && events[i].error == E::kNone);
  frame.receipt_ns = Now() - 4000000000LL;
  assert(!publisher.Send(frame, Now()));
  frame.receipt_ns = Now();
  assert(publisher.Send(frame, Now()));
  assert(publisher.Send(frame, Now()));
  server.join();
  publisher.Tick(Now());
  assert(!publisher.streaming());
  assert(events.back().kind == K::kFailure && events.back().error == E::kRead);
  assert(publisher.metrics().frames == 2 && publisher.metrics().expired == 1);
  publisher.Stop();
  assert(events.back().kind == K::kStopped);
  const auto stopped_size = events.size();
  auto invalid = config;
  invalid.password = "bad\r\nheader";
  assert(!publisher.Configure(invalid, true));
  assert(events.size() == stopped_size);
  for (bool timeout_case : {false, true})
  {
    events.clear();
    std::atomic<bool> release{false};
    std::thread rejected([&] {
      const int fd = accept(listener, nullptr, nullptr);
      assert(fd >= 0);
      timeval timeout{2, 0};
      setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      char data[1024];
      assert(recv(fd, data, sizeof(data), 0) > 0);
      if (!timeout_case)
      {
        const std::string denied = "ERROR - Bad Password\r\n";
        assert(send(fd, denied.data(), denied.size(), 0) == static_cast<ssize_t>(denied.size()));
      }
      const auto end = Now() + 2000000000LL;
      while (!release && Now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      close(fd);
    });
    assert(publisher.Configure(config, true));
    const auto end = Now() + 1500000000LL;
    while ((events.empty() || events.back().kind != K::kRequestSent) && Now() < end)
      publisher.Tick(Now());
    assert(events.back().kind == K::kRequestSent);
    while (events.back().kind != K::kFailure && Now() < end)
      publisher.Tick(Now() + (timeout_case ? 6000000000LL : 0));
    release = true;
    rejected.join();
    assert(events.back().kind == K::kFailure &&
           events.back().error == (timeout_case ? E::kTimeout : E::kProtocol));
    for (const auto& event : events)
      assert(event.kind != K::kAccepted);
    publisher.Stop();
  }
  bool cancelled = true;
  assert(publisher.Configure(config, true, [&] { return cancelled; }));
  publisher.Tick(Now());
  assert(!publisher.streaming());
  assert(events.back().kind == K::kFailure && events.back().error == E::kCancelled);
  close(listener);
}
