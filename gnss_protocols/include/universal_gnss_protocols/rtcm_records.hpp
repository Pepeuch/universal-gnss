#pragma once

#include <cstdint>
#include <optional>

namespace universal_gnss_protocols
{

enum class RtcmConstellation : std::uint8_t
{
  kUnknown = 0,
  kGps = 1,
  kGlonass = 2,
  kGalileo = 3,
  kSbas = 4,
  kQzss = 5,
  kBeiDou = 6,
  kNavIc = 7,
};

struct RtcmMessageInfo
{
  std::uint16_t message_type{0};
  bool is_station_arp{false};
  bool is_glonass_bias{false};
  bool is_msm{false};
  RtcmConstellation msm_constellation{RtcmConstellation::kUnknown};
  std::uint8_t msm_variant{0};
};

struct RtcmBaseStationArpRecord
{
  std::uint16_t message_type{0};
  std::uint16_t station_id{0};
  std::uint8_t itrf_year{0};
  bool gps_indicator{false};
  bool glonass_indicator{false};
  bool galileo_indicator{false};
  bool reference_station_indicator{false};
  double ecef_x_m{0.0};
  double ecef_y_m{0.0};
  double ecef_z_m{0.0};
  std::optional<double> antenna_height_m{};
  bool single_receiver_oscillator_indicator{false};
  std::uint8_t quarter_cycle_indicator{0};
};

struct RtcmGlonassCodePhaseBiasRecord
{
  std::uint16_t message_type{0};
  std::uint16_t station_id{0};
  bool code_phase_bias_indicator{false};
  std::uint8_t signal_mask{0};
  bool has_any_bias_values{false};
  bool valid{false};
  std::optional<double> l1_ca_bias_m{};
  std::optional<double> l1_p_bias_m{};
  std::optional<double> l2_ca_bias_m{};
  std::optional<double> l2_p_bias_m{};
};

struct RtcmMsmSummaryRecord
{
  std::uint16_t message_type{0};
  std::uint16_t station_id{0};
  RtcmConstellation constellation{RtcmConstellation::kUnknown};
  std::uint8_t msm_variant{0};
  bool multiple_message{false};
  std::uint8_t issue_of_data_station{0};
  std::uint8_t session_transmission_time{0};
  std::uint8_t clock_steering_indicator{0};
  std::uint8_t external_clock_indicator{0};
  bool divergence_free_smoothing{false};
  std::uint8_t smoothing_interval{0};
  std::uint8_t satellite_count{0};
  std::uint8_t signal_count{0};
  std::uint16_t cell_count{0};
};

}  // namespace universal_gnss_protocols
