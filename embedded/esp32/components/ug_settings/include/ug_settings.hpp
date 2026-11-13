// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss_ntrip/ntrip_config.hpp"
#include <array>
#include <memory>
#include <string_view>
namespace ug::embedded
{
struct Capabilities
{
  bool local_gnss{false}, caster{false}, lora{false};
  std::size_t publisher_slots{0};
  std::string board{}, role{};
};
struct PublisherSettings
{
  bool enabled{false};
  universal_gnss_ntrip::NtripConfig endpoint{};
};
struct Settings
{
  static constexpr unsigned kSchema = 1;
  std::uint64_t revision{1};  // In-memory apply generation; not a physical observation.
  std::string device_name{"ug-rtk"};
  bool local_input{false}, gnss_enabled{false}, upstream_enabled{false};
  std::uint32_t gnss_baud{115200};
  std::string gnss_model{"auto"};
  universal_gnss_ntrip::NtripConfig upstream{};
  std::array<PublisherSettings, 5> publishers{};
  bool caster_enabled{false};
  std::uint16_t caster_port{2101};
  std::string caster_mountpoint{"RTCM3"}, caster_user{}, caster_password{};
  bool wifi_sta_enabled{false}, wifi_ap_enabled{false};
  std::string wifi_ssid{}, wifi_password{}, ap_ssid{"UG-RTK"}, ap_password{};
  std::string ntp_server{"pool.ntp.org"};
  bool radio_enabled{true};
  std::uint8_t radio_channel{1};
  std::string radio_peer{};          // Empty uses the compile-time development peer only.
  std::string admin_token_sha256{};  // Blank locks administration; never a plaintext token.
  bool lora_enabled{false};
  std::string lora_region{"EU868"};
  std::uint32_t lora_frequency_hz{868100000};
  std::uint8_t lora_sf{7}, lora_coding_rate{5};
  std::uint32_t lora_bandwidth_hz{125000};
  std::int8_t lora_power_dbm{14};
};
Settings DefaultSettings(const Capabilities& capabilities);
bool ValidateSettings(const Settings&, const Capabilities&, std::string& error);
// Patch an immutable prior snapshot; unknown/duplicate fields and invalid values fail atomically.
bool DecodeSettings(std::string_view json,
                    const Settings& prior,
                    const Capabilities&,
                    Settings& result,
                    std::string& error);
// include_secrets is for local persistence only, never selected by an HTTP query parameter.
bool EncodeSettings(const Settings&, std::string& json, bool include_secrets = false);
}  // namespace ug::embedded
