#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "universal_gnss_ntrip/local_rtcm_caster.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"

namespace {
using universal_gnss_ntrip::LocalRtcmCaster;
using universal_gnss_ntrip::LocalRtcmCasterConfig;
using universal_gnss_ntrip::LocalRtcmSourceIdentity;
using universal_gnss_protocols::ChecksumStatus;
using universal_gnss_protocols::RtcmFrame;

struct TestContext
{
  int failures{0};
  void Expect(bool ok, const std::string& text)
  {
    if (!ok)
    {
      ++failures;
      std::cerr << "FAILED: " << text << '\n';
    }
  }
};

RtcmFrame Frame(const std::uint16_t type)
{
  RtcmFrame frame;
  frame.message_type = type;
  frame.payload = {static_cast<std::uint8_t>(type >> 4u), static_cast<std::uint8_t>(type << 4u)};
  frame.raw_bytes = {0xD3u, 0u, 2u, frame.payload[0], frame.payload[1]};
  const std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(frame.raw_bytes.data(), frame.raw_bytes.size());
  frame.raw_bytes.push_back(static_cast<std::uint8_t>(crc >> 16u));
  frame.raw_bytes.push_back(static_cast<std::uint8_t>(crc >> 8u));
  frame.raw_bytes.push_back(static_cast<std::uint8_t>(crc));
  frame.checksum_status = ChecksumStatus::kValid;
  return frame;
}

int Connect(const std::uint16_t port)
{
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  return fd >= 0 && ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0
             ? fd
             : -1;
}

std::string ReadAll(int fd)
{
  char bytes[512]{};
  const ssize_t count = ::recv(fd, bytes, sizeof(bytes), MSG_DONTWAIT);
  return count > 0 ? std::string(bytes, static_cast<std::size_t>(count)) : std::string{};
}

void TestServingAndIncarnation(TestContext& ctx)
{
  LocalRtcmCaster caster;
  LocalRtcmCasterConfig config;
  config.mountpoint = "BASE";
  ctx.Expect(caster.Start(config), "caster should start");
  ctx.Expect(caster.ActivateSource({"receiver-a", 1u}), "first source incarnation should activate");
  const RtcmFrame base = Frame(1005u);
  const RtcmFrame msm = Frame(1077u);
  ctx.Expect(caster.PublishFrame(base) && caster.PublishFrame(msm),
             "active source should publish valid RTCM");
  const int one = Connect(caster.port());
  ctx.Expect(one >= 0, "first client should connect");
  const std::string request = "GET /BASE HTTP/1.0\r\n\r\n";
  (void)::send(one, request.data(), request.size(), 0);
  for (int i = 0; i < 5; ++i)
    caster.Poll();
  const std::string initial = ReadAll(one);
  ctx.Expect(initial.find("ICY 200 OK") != std::string::npos &&
                 initial.find(static_cast<char>(0xD3u)) != std::string::npos,
             "new client gets ICY and active-incarnation static base cache");
  caster.EndSource();
  const int two = Connect(caster.port());
  (void)::send(two, request.data(), request.size(), 0);
  for (int i = 0; i < 5; ++i)
    caster.Poll();
  const std::string after_end = ReadAll(two);
  ctx.Expect(after_end == "ICY 200 OK\r\n\r\n",
             "ended source must expose no stale static or dynamic RTCM");
  ctx.Expect(caster.ActivateSource({"receiver-a", 2u}) &&
                 !caster.ActivateSource({"receiver-a", 2u}),
             "restart requires a new explicit incarnation");
  ::close(one);
  ::close(two);
  caster.Stop();
}
} // namespace
int main()
{
  TestContext ctx;
  TestServingAndIncarnation(ctx);
  return ctx.failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
