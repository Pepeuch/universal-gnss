// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_wire_selftest.hpp"
#include "ug_json.hpp"
#include "ug_rtcm.h"
extern "C" {
#include "espnow_rtcm_codec.h"
}
#include <array>
#include <cstring>

namespace ug::embedded
{
namespace
{
using Frame = std::array<std::uint8_t, 1029>;
bool Stamp(Frame& frame)
{
  std::uint32_t crc{};
  if (!ug_rtcm_crc24q(frame.data(), frame.size() - 3, &crc))
    return false;
  frame[frame.size() - 3] = crc >> 16;
  frame[frame.size() - 2] = crc >> 8;
  frame.back() = crc;
  return true;
}
struct Loopback
{
  espnow_rtcm_codec_t receiver{};
  const Frame* expected{};
  unsigned mode{}, packets{}, completed{}, errors{};
  std::uint32_t clock_ms{};
  bool matching{true};
};
esp_err_t Receive(const lora_transport_fragment_t* fragment, void* opaque)
{
  auto& loop = *static_cast<Loopback*>(opaque);
  ++loop.packets;
  if (loop.mode == 1 && fragment->fragment_index == 1)
    return ESP_OK;  // Simulated missing middle fragment; never transmitted.
  if (!fragment->packet || fragment->packet_len > ESPNOW_RTCM_PACKET_SIZE ||
      fragment->packet_len < 15)
    return ESP_FAIL;
  std::array<std::uint8_t, ESPNOW_RTCM_PACKET_SIZE> packet{};
  std::memcpy(packet.data(), fragment->packet, fragment->packet_len);
  if (loop.mode == 2 && fragment->fragment_index == 0)
    packet[14] ^= 1;  // CRC16 corruption.
  if (loop.mode == 3 && fragment->fragment_index == 0)
  {
    packet[14] ^= 1;  // RTCM corruption with repaired radio integrity.
    const auto crc = lora_transport_crc16(packet.data(), fragment->packet_len - 2);
    packet[fragment->packet_len - 2] = crc >> 8;
    packet[fragment->packet_len - 1] = crc;
  }
  if (loop.mode == 1 && fragment->fragment_index == 2)
    loop.clock_ms += ESPNOW_RTCM_REASSEMBLY_MS + 1;
  lora_transport_reassembly_result_t result{};
  const auto status = espnow_rtcm_receive(
      &loop.receiver, packet.data(), fragment->packet_len, ++loop.clock_ms, &result);
  if (status != ESP_OK)
    ++loop.errors;
  if (result.complete)
  {
    ++loop.completed;
    loop.matching = loop.matching && status == ESP_OK && result.frame &&
                    result.frame_len == loop.expected->size() &&
                    ug_rtcm_frame_valid(result.frame, result.frame_len) &&
                    std::memcmp(result.frame, loop.expected->data(), result.frame_len) == 0;
  }
  // Expected negative codec results are counted, never hidden by send success.
  return ESP_OK;
}
bool Radio(const Frame& frame, unsigned mode)
{
  espnow_rtcm_codec_t sender{};
  espnow_rtcm_codec_init(&sender);
  Loopback loop;
  loop.expected = &frame;
  loop.mode = mode;
  espnow_rtcm_codec_init(&loop.receiver);
  if (espnow_rtcm_fragment(&sender, frame.data(), frame.size(), Receive, &loop) != ESP_OK ||
      loop.packets < 3 || !loop.matching)
    return false;
  if (mode == 0)
    return loop.completed == 1 && loop.errors == 0;
  if (loop.completed != 0)
    return false;
  if (mode == 1)
    return loop.receiver.reassembly_expired == 1;
  return loop.errors > 0 &&
         (mode == 2 ? loop.receiver.crc16_errors == 1 : loop.receiver.crc24q_errors == 1);
}
struct Stream
{
  const Frame* expected;
  unsigned completed{};
  bool matching{true};
};
esp_err_t Accept(const std::uint8_t* bytes, std::size_t size, void* opaque)
{
  auto& stream = *static_cast<Stream*>(opaque);
  ++stream.completed;
  stream.matching = stream.matching && bytes && size == stream.expected->size() &&
                    std::memcmp(bytes, stream.expected->data(), size) == 0;
  return ESP_OK;
}
bool Framing(const Frame& frame)
{
  espnow_rtcm_codec_t codec{};
  espnow_rtcm_codec_init(&codec);
  Stream sink{&frame};
  constexpr std::array<std::uint8_t, 3> noise{0, 0x42, 0xff};
  if (espnow_rtcm_stream_push(&codec, noise.data(), noise.size(), Accept, &sink) != ESP_OK)
    return false;
  for (std::size_t offset = 0; offset < frame.size(); ++offset)
    if (espnow_rtcm_stream_push(&codec, &frame[offset], 1, Accept, &sink) != ESP_OK)
      return false;
  return sink.completed == 1 && sink.matching;
}
}  // namespace
const char* WireSelfTestName(unsigned index)
{
  constexpr const char* names[kWireSelfTestCount] = {"crc_vectors",
                                                     "rtcm_container_bounds",
                                                     "split_stream",
                                                     "radio_codec_loopback",
                                                     "missing_fragment_expiry",
                                                     "corrupt_fragment_rejection"};
  return index < kWireSelfTestCount ? names[index] : "invalid";
}
bool DecodeWireSelfTest(std::string_view body)
{
  std::string error;
  auto root = ParseBoundedObject(body, error);
  if (!root || !OnlyKeys(root.get(), {"confirm"}))
    return false;
  const auto* confirm = cJSON_GetObjectItemCaseSensitive(root.get(), "confirm");
  return cJSON_IsString(confirm) &&
         std::strcmp(confirm->valuestring, "RUN_OFFLINE_WIRE_SELFTEST") == 0;
}
WireSelfTestReport RunWireSelfTest()
{
  WireSelfTestReport out;
  auto record = [&](unsigned index, bool passed) {
    (passed ? out.passed : out.failed) |= 1u << index;
  };
  constexpr std::uint8_t vector[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  std::uint32_t crc{};
  record(0,
         ug_rtcm_crc24q(vector, sizeof(vector), &crc) && crc == 0xcde703 &&
             lora_transport_crc16(vector, sizeof(vector)) == 0x29b1);
  Frame frame{};
  frame[0] = 0xd3;
  frame[1] = 3;
  frame[2] = 0xff;
  for (std::size_t i = 3; i < frame.size() - 3; ++i)
    frame[i] = static_cast<std::uint8_t>(i * 17);
  if (!Stamp(frame))
  {
    out.failed |= kWireSelfTestMask & ~out.passed;
    return out;
  }
  bool valid = ug_rtcm_frame_valid(frame.data(), frame.size()) &&
               !ug_rtcm_frame_valid(frame.data(), frame.size() - 1) &&
               !ug_rtcm_frame_valid(nullptr, 0);
  frame.back() ^= 1;
  valid = valid && !ug_rtcm_frame_valid(frame.data(), frame.size());
  frame.back() ^= 1;
  frame[1] |= 0x80;
  const bool reserved_stamped = Stamp(frame);
  valid = valid && reserved_stamped && !ug_rtcm_frame_valid(frame.data(), frame.size());
  frame[1] &= ~0x80;
  frame[2] ^= 1;
  const bool length_stamped = Stamp(frame);
  valid = valid && length_stamped && !ug_rtcm_frame_valid(frame.data(), frame.size());
  frame[2] ^= 1;
  valid = Stamp(frame) && valid;
  record(1, valid);
  record(2, Framing(frame));
  record(3, Radio(frame, 0));
  record(4, Radio(frame, 1));
  const bool outer = Radio(frame, 2), inner = Radio(frame, 3);
  record(5, outer && inner);
  return out;
}
}  // namespace ug::embedded
