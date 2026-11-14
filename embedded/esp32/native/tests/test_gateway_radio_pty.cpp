// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_gateway.hpp"
#include "ug_ntrip_connection.hpp"
#include "ug_rtcm.h"
extern "C" {
#include "espnow_rtcm_codec.h"
}
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <pty.h>
#include <sys/socket.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
using ug::embedded::Gateway;
static std::int64_t Now()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
static std::vector<std::uint8_t> Frame(unsigned payload)
{
  std::vector<std::uint8_t> f(payload + 6);
  f[0] = 0xd3;
  f[1] = payload >> 8;
  f[2] = payload;
  for (unsigned i = 0; i < payload; ++i)
    f[3 + i] = static_cast<std::uint8_t>(i * 37 + payload);
  std::uint32_t crc;
  assert(ug_rtcm_crc24q(f.data(), f.size() - 3, &crc));
  f[f.size() - 3] = crc >> 16;
  f[f.size() - 2] = crc >> 8;
  f.back() = crc;
  return f;
}
struct Radio
{
  espnow_rtcm_codec_t base{}, rover{};
  int usb_fd{-1};
  unsigned complete{}, rejected{}, packets{};
  bool drop = false, corrupt = false, repair_crc16 = false;
  Radio()
  {
    espnow_rtcm_codec_init(&base);
    espnow_rtcm_codec_init(&rover);
  }
  static esp_err_t Packet(const lora_transport_fragment_t* part, void* context)
  {
    auto& self = *static_cast<Radio*>(context);
    ++self.packets;
    if (self.drop && part->fragment_index == 1)
      return ESP_OK;
    std::vector<std::uint8_t> packet(part->packet, part->packet + part->packet_len);
    if (self.corrupt && part->fragment_index == 0)
    {
      packet[14] ^= 1;  // RTCM payload byte, preserve both framing headers.
      if (self.repair_crc16)
      {
        const auto crc = lora_transport_crc16(packet.data(), packet.size() - 2);
        packet[packet.size() - 2] = crc >> 8;
        packet.back() = crc;
      }
    }
    lora_transport_reassembly_result_t result;
    const auto status = espnow_rtcm_receive(&self.rover, packet.data(), packet.size(), 1, &result);
    if (status != ESP_OK)
      ++self.rejected;
    else if (result.complete)
    {
      assert(ug_rtcm_frame_valid(result.frame, result.frame_len));
      ++self.complete;
      assert(write(self.usb_fd, result.frame, result.frame_len) ==
             static_cast<ssize_t>(result.frame_len));
    }
    return ESP_OK;  // Injection models missing/corrupt delivery, not a physical MAC ACK.
  }
  void Send(const Gateway::Frame& f)
  {
    assert(espnow_rtcm_fragment(&base, f.bytes.data(), f.size, Packet, this) == ESP_OK);
  }
};
static void Scenario(bool drop, bool corrupt, bool repair_crc16 = false)
{
  const auto first = Frame(20), large = Frame(1023), last = Frame(300);
  auto invalid = Frame(44);
  invalid.back() ^= 1;
  std::vector<std::uint8_t> payload(first);
  payload.insert(payload.end(), large.begin(), large.end());
  payload.insert(payload.end(), invalid.begin(), invalid.end());
  payload.insert(payload.end(), last.begin(), last.end());
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  socklen_t length = sizeof(address);
  assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) == 0);
  assert(listen(listener, 1) == 0);
  std::atomic<bool> done{false};
  std::thread server([&] {
    const auto fd = accept(listener, nullptr, nullptr);
    assert(fd >= 0);
    timeval timeout{2, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    std::string request;
    char bytes[512];
    while (request.find("\r\n\r\n") == std::string::npos)
    {
      auto n = recv(fd, bytes, sizeof(bytes), 0);
      assert(n > 0);
      request.append(bytes, n);
    }
    assert(request.find("GET /RTCM HTTP/1.1") == 0);
    const std::string header = "HTTP/1.1 200 OK\r\nContent-Type: gnss/data\r\n\r\n";
    std::vector<std::uint8_t> stream(header.begin(), header.end());
    stream.insert(stream.end(), payload.begin(), payload.end());
    for (std::size_t i = 0; i < stream.size();)
    {
      const auto size = std::min<std::size_t>(1 + i % 31, stream.size() - i);
      auto n = send(fd, stream.data() + i, size, 0);
      assert(n > 0);
      i += n;
    }
    const auto deadline = Now() + 3000000000LL;
    while (!done && Now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    close(fd);
  });
  int master, slave;
  termios terminal{};
  assert(openpty(&master, &slave, nullptr, nullptr, nullptr) == 0);
  assert(tcgetattr(slave, &terminal) == 0);
  cfmakeraw(&terminal);
  assert(tcsetattr(slave, TCSANOW, &terminal) == 0);
  assert(fcntl(slave, F_SETFL, O_NONBLOCK) == 0);
  Radio radio;
  radio.usb_fd = master;
  radio.repair_crc16 = repair_crc16;
  Gateway gateway;
  Gateway::Frame frame;
  assert(gateway.ConfigureSink(0, true, 3000000000LL));
  std::uint64_t incarnation = 0;
  unsigned received = 0;
  ug::embedded::NtripConnection input(
      [&](const auto& observed) {
        assert(observed.timestamp_ns);
        ++received;
        assert(gateway.FeedFrame(1,
                                 incarnation,
                                 observed.raw_bytes.data(),
                                 observed.raw_bytes.size(),
                                 *observed.timestamp_ns));
      },
      [&](bool active) { assert(gateway.ConfigureSource(1, ++incarnation, active)); });
  universal_gnss_ntrip::NtripConfig config;
  config.host = "127.0.0.1";
  config.port = ntohs(address.sin_port);
  config.mountpoint = "RTCM";
  assert(input.Configure(config, true));
  const auto deadline = Now() + 2000000000LL;
  while (received < 3 && Now() < deadline)
  {
    input.Tick(Now());
    while (gateway.Pop(0, Now(), frame))
    {
      radio.drop = drop && frame.size == large.size();
      radio.corrupt = corrupt && frame.size == large.size();
      radio.Send(frame);
    }
  }
  assert(received == 3);  // Corrupted upstream RTCM never enters the radio path.
  std::vector<std::uint8_t> expected = first;
  if (!drop && !corrupt)
    expected.insert(expected.end(), large.begin(), large.end());
  expected.insert(expected.end(), last.begin(), last.end());
  std::vector<std::uint8_t> capture;
  std::uint8_t bytes[2048];
  while (capture.size() < expected.size() && Now() < deadline)
  {
    auto n = read(slave, bytes, sizeof(bytes));
    if (n > 0)
      capture.insert(capture.end(), bytes, bytes + n);
    else
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(capture == expected);
  assert(radio.complete == ((drop || corrupt) ? 2U : 3U));
  if (drop)
    assert(radio.rover.reassembly_abandoned > 0);
  if (corrupt)
  {
    assert(radio.rejected > 0);
    if (repair_crc16)
      assert(radio.rover.crc24q_errors > 0);
    else
      assert(radio.rover.crc16_errors > 0);
  }
  done = true;
  server.join();
  input.Stop();
  assert(!gateway.Pop(0, Now(), frame));
  close(listener);
  close(master);
  close(slave);
}
int main()
{
  Scenario(false, false);
  Scenario(true, false);
  Scenario(false, true);
  Scenario(false, true, true);
}
