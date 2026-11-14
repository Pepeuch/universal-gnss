// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss_protocols/nmea_records.hpp"
#include "universal_gnss_protocols/protocol_records.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"
#include "universal_gnss_protocols/unicore_records.hpp"
#include <array>
#include <string_view>
namespace ug::embedded
{
enum class SatelliteProtocol : std::uint8_t
{
  kNone,
  kNmeaGsv,
  kUbxNavSat,
  kUnicoreSatsInfo
};
struct SatelliteEntry
{
  enum : std::uint8_t
  {
    kElevation = 1,
    kAzimuth = 2,
    kCn0 = 4,
    kUsedKnown = 8,
    kUsed = 16,
    kHealthKnown = 32,
    kHealthy = 64
  };
  float cn0{};
  std::uint16_t id{};
  std::int16_t elevation{}, azimuth{};
  std::uint8_t raw_system_id{255}, flags{}, frequency_status{}, frequency_count{};
};
struct SatelliteReport
{
  static constexpr std::size_t kCapacity = 64;
  SatelliteProtocol protocol{SatelliteProtocol::kNone};
  std::array<char, 3> talker{};
  std::optional<std::int64_t> receipt_ns{};
  std::optional<std::uint8_t> signal_id{};
  std::uint32_t tow_ms{};
  std::uint16_t week{}, reported_total{};
  std::uint8_t time_reference{};
  bool epoch_known{}, truncated{};
  std::size_t count{};
  std::array<SatelliteEntry, kCapacity> entries{};
};
inline bool SatelliteReportFresh(const SatelliteReport& report, std::int64_t now)
{
  return report.receipt_ns && *report.receipt_ns >= 0 && now >= *report.receipt_ns &&
         now - *report.receipt_ns <= 3000000000LL;
}
// Read only the trailing scope metadata absent from the public UG GSV record;
// satellite values and checksum validation remain in the existing UG parser.
bool GsvSignalScope(const universal_gnss_protocols::NmeaSentence&, std::optional<std::uint8_t>&);
// Single UART owner. Bounded report cache, never a position/runtime-state producer.
// Protocol decoding/CRC remains exclusively in the existing UG parsers.
class SatelliteReports
{
public:
  void Observe(const universal_gnss_protocols::UbxNavSatRecord&);
  void Observe(const universal_gnss_protocols::UnicoreSatsInfoRecord&);
  void Observe(const universal_gnss_protocols::NmeaGsvRecord&,
               std::string_view talker,
               std::optional<std::uint8_t> signal_id = {});
  const SatelliteReport& Latest(std::int64_t now) const;
  void Reset();

private:
  struct Cycle
  {
    SatelliteReport partial{};
    std::uint8_t expected{}, total{};
    bool active{};
  };
  void PublishVendor(SatelliteReport&&);
  std::array<SatelliteReport, 2> vendors_{};
  SatelliteReport nmea_{};
  std::array<Cycle, 8> cycles_{};
};
}  // namespace ug::embedded
