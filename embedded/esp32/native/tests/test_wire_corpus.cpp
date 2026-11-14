// SPDX-License-Identifier: LGPL-3.0-only
// Bounded, deterministic mutation corpus. This is not exhaustive fuzzing or RF evidence.
#include "ug_rtcm.h"
extern "C" {
#include "espnow_rtcm_codec.h"
}
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>
using Bytes = std::vector<std::uint8_t>;
static std::uint32_t seed = 0x47554e53;
static std::uint32_t Random()
{
  seed = 1664525u * seed + 1013904223u;
  return seed;
}
static Bytes Frame(unsigned payload)
{
  Bytes bytes(payload + 6);
  bytes[0] = 0xd3;
  bytes[1] = payload >> 8;
  bytes[2] = payload;
  for (unsigned i = 0; i < payload; ++i)
    bytes[3 + i] = Random() >> 24;
  std::uint32_t crc = 0;
  assert(ug_rtcm_crc24q(bytes.data(), bytes.size() - 3, &crc));
  bytes[bytes.size() - 3] = crc >> 16;
  bytes[bytes.size() - 2] = crc >> 8;
  bytes.back() = crc;
  return bytes;
}
static esp_err_t Packet(const lora_transport_fragment_t* packet, void* context)
{
  auto& packets = *static_cast<std::vector<Bytes>*>(context);
  packets.emplace_back(packet->packet, packet->packet + packet->packet_len);
  return ESP_OK;
}
static void RepairRadioIntegrity(Bytes& packet)
{
  assert(packet.size() >= 13);
  std::uint8_t sum = 0;
  for (unsigned i = 0; i < 10; ++i)
    sum += packet[i];
  packet[10] = sum;
  const auto crc = lora_transport_crc16(packet.data(), packet.size() - 2);
  packet[packet.size() - 2] = crc >> 8;
  packet.back() = crc;
}
struct Sink
{
  std::vector<Bytes> frames;
  static esp_err_t Accept(const std::uint8_t* bytes, std::size_t size, void* context)
  {
    assert(size >= 6 && size <= RTCM3_MAX_FRAME_LEN);
    assert(ug_rtcm_frame_valid(bytes, size));
    static_cast<Sink*>(context)->frames.emplace_back(bytes, bytes + size);
    return ESP_OK;
  }
};
int main()
{
  espnow_rtcm_codec_t sender{}, receiver{};
  espnow_rtcm_codec_init(&sender);
  unsigned valid_radio_frames = 0;
  std::uint32_t clock_ms = UINT32_MAX - 100;  // Cross the wrapping millisecond boundary.
  for (unsigned trial = 0; trial < 2400; ++trial)
  {
    espnow_rtcm_codec_init(&receiver);
    const auto source = Frame(500 + Random() % 524);
    std::vector<Bytes> packets;
    assert(espnow_rtcm_fragment(&sender, source.data(), source.size(), Packet, &packets) == ESP_OK);
    assert(packets.size() >= 3);
    const auto mode = trial % 6;
    if (mode == 1)
      packets.erase(packets.begin() + Random() % packets.size());
    if (mode == 2 || mode == 3)
    {
      packets[0][14] ^= 1;  // Corrupt RTCM payload, optionally pass radio CRC to reach UG CRC24Q.
      if (mode == 3)
        RepairRadioIntegrity(packets[0]);
    }
    unsigned completed = 0;
    for (std::size_t index = 0; index < packets.size(); ++index)
    {
      const auto& packet = packets[index];
      if (mode == 5 && index)
        clock_ms += ESPNOW_RTCM_REASSEMBLY_MS + 1;
      clock_ms += 1 + Random() % 10;
      for (unsigned duplicate = 0; duplicate < (mode == 4 ? 2u : 1u); ++duplicate)
      {
        lora_transport_reassembly_result_t result{};
        const auto status =
            espnow_rtcm_receive(&receiver, packet.data(), packet.size(), clock_ms, &result);
        if (status == ESP_OK && result.complete)
        {
          assert(ug_rtcm_frame_valid(result.frame, result.frame_len));
          assert(Bytes(result.frame, result.frame + result.frame_len) == source);
          ++completed;
        }
      }
    }
    assert(completed == ((mode == 0 || mode == 4) ? 1u : 0u));
    valid_radio_frames += completed;
  }
  assert(valid_radio_frames == 800);  // Positive coverage, not only rejected noise.
  // Stateful structurally plausible mutations, including repaired outer integrity.
  espnow_rtcm_codec_init(&receiver);
  std::vector<Bytes> templates;
  const auto source = Frame(1023);
  assert(espnow_rtcm_fragment(&sender, source.data(), source.size(), Packet, &templates) == ESP_OK);
  for (unsigned trial = 0; trial < 12000; ++trial)
  {
    auto packet = templates[Random() % templates.size()];
    for (unsigned mutation = 0, count = 1 + Random() % 4; mutation < count; ++mutation)
      packet[Random() % packet.size()] ^= 1u << (Random() % 8);
    if (trial % 3 == 0)
      RepairRadioIntegrity(packet);
    if (trial % 5 == 0)
      packet.resize(Random() % (packet.size() + 1));
    clock_ms += Random() % 700;
    lora_transport_reassembly_result_t result{};
    const auto status =
        espnow_rtcm_receive(&receiver, packet.data(), packet.size(), clock_ms, &result);
    if (status == ESP_OK && result.complete)
      assert(ug_rtcm_frame_valid(result.frame, result.frame_len));
    assert(!result.complete || result.frame_len <= RTCM3_MAX_FRAME_LEN);
  }
  // Arbitrary bytes and split framing; no emitted container may bypass UG integrity.
  Sink sink;
  espnow_rtcm_codec_init(&receiver);
  for (unsigned trial = 0; trial < 2000; ++trial)
  {
    Bytes bytes(Random() % 1200);
    for (auto& byte : bytes)
      byte = Random() >> 24;
    (void)espnow_rtcm_stream_push(&receiver, bytes.data(), bytes.size(), Sink::Accept, &sink);
  }
  for (unsigned payload : {0u, 1u, 2u, 20u, 206u, 207u, 208u, 1023u})
  {
    espnow_rtcm_stream_reset(&receiver);
    const auto valid = Frame(payload);
    sink.frames.clear();
    for (std::size_t offset = 0; offset < valid.size();)
    {
      const auto count = std::min<std::size_t>(1 + Random() % 37, valid.size() - offset);
      assert(espnow_rtcm_stream_push(
                 &receiver, valid.data() + offset, count, Sink::Accept, &sink) == ESP_OK);
      offset += count;
    }
    assert(sink.frames.size() == 1 && sink.frames.front() == valid);
  }
}
