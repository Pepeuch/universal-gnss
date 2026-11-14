// SPDX-License-Identifier: LGPL-3.0-only
#include "universal_gnss_ntrip/local_rtcm_caster.hpp"
#include "universal_gnss_ntrip/ntrip_request.hpp"
#include <cassert>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
using namespace universal_gnss_ntrip;
static int Connect(std::uint16_t port)
{
  auto fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons(port);
  assert(connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0);
  return fd;
}
static std::string PollRead(LocalRtcmCaster& caster, int fd)
{
  for (int i = 0; i < 10; ++i)
    caster.Poll();
  char bytes[1024];
  auto n = recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
  return n > 0 ? std::string(bytes, n) : std::string{};
}
int main()
{
  LocalRtcmCaster caster;
  LocalRtcmCasterPolicy policy;
  policy.username = "tester";
  policy.password = "not-a-real-secret";
  policy.max_clients = 1;
  policy.max_accepts_per_poll = 1;
  policy.request_timeout_ms = 10;
  policy.disconnect_clients_on_source_change = true;
  auto invalid = policy;
  invalid.password.clear();
  assert(!caster.SetPolicy(invalid));
  assert(caster.SetPolicy(policy));
  LocalRtcmCasterConfig cfg;
  cfg.mountpoint = "BASE";
  assert(caster.Start(cfg));
  assert(!caster.SetPolicy(policy));
  assert(caster.ActivateSource({"test", 1}));
  int fd = Connect(caster.port());
  const std::string noauth = "GET /BASE HTTP/1.0\r\n\r\n";
  assert(send(fd, noauth.data(), noauth.size(), 0) == static_cast<ssize_t>(noauth.size()));
  const auto rejected = PollRead(caster, fd);
  assert(rejected.find("200 OK") == std::string::npos);
  close(fd);
  const std::string authorization = "Authorization: Basic " +
                                    BuildBasicAuthorizationValue(policy.username, policy.password) +
                                    "\r\n";
  fd = Connect(caster.port());
  auto partial = "GET /BASE HTTP/1.0\r\n" + authorization;
  assert(send(fd, partial.data(), partial.size(), 0) == static_cast<ssize_t>(partial.size()));
  assert(PollRead(caster, fd).empty());  // A header prefix cannot authorize before full headers.
  assert(send(fd, "\r\n", 2, 0) == 2);
  assert(PollRead(caster, fd).find("ICY 200 OK") != std::string::npos);
  int overflow = Connect(caster.port());
  assert(PollRead(caster, overflow).empty());
  char c;
  assert(recv(overflow, &c, 1, MSG_DONTWAIT) == 0);
  close(overflow);
  caster.EndSource();
  assert(recv(fd, &c, 1, MSG_DONTWAIT) == 0);
  close(fd);
  fd = Connect(caster.port());
  caster.Poll();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  caster.Poll();
  assert(recv(fd, &c, 1, MSG_DONTWAIT) == 0);
  close(fd);
  fd = Connect(caster.port());
  auto duplicate = "GET /BASE HTTP/1.0\r\n" + authorization + authorization + "\r\n";
  assert(send(fd, duplicate.data(), duplicate.size(), 0) == static_cast<ssize_t>(duplicate.size()));
  assert(PollRead(caster, fd).find("200 OK") == std::string::npos);
  close(fd);
  caster.Stop();
}
