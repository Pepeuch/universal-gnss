#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace universal_gnss_ntrip
{

struct NtripSourcetableStream
{
  std::size_t line_number{0u};
  std::string mountpoint{};
  std::optional<std::string> identifier{};
  std::optional<std::string> format{};
  std::optional<std::string> format_details{};
  std::optional<std::uint32_t> carrier{};
  std::optional<std::string> nav_system{};
  std::optional<std::string> network{};
  std::optional<std::string> country{};
  std::optional<double> latitude_deg{};
  std::optional<double> longitude_deg{};
  std::optional<bool> nmea_required{};
  std::optional<std::uint32_t> solution{};
  std::optional<std::string> generator{};
  std::optional<std::string> compression{};
  std::optional<std::string> authentication{};
  std::optional<bool> fee{};
  std::optional<std::uint32_t> bitrate{};
  std::vector<std::string> fields{};
};

struct NtripSourcetableCaster
{
  std::size_t line_number{0u};
  std::optional<std::string> host{};
  std::optional<std::uint16_t> port{};
  std::optional<std::string> identifier{};
  std::vector<std::string> fields{};
};

struct NtripSourcetableNetwork
{
  std::size_t line_number{0u};
  std::optional<std::string> identifier{};
  std::vector<std::string> fields{};
};

enum class NtripSourcetableIssueCode : std::uint8_t
{
  kUnknownRecord = 0,
  kMissingMountpoint = 1,
  kInvalidCarrier = 2,
  kInvalidLatitude = 3,
  kInvalidLongitude = 4,
  kInvalidNmeaFlag = 5,
  kInvalidSolution = 6,
  kInvalidFeeFlag = 7,
  kInvalidBitrate = 8,
  kInvalidCasterPort = 9,
};

struct NtripSourcetableIssue
{
  std::size_t line_number{0u};
  NtripSourcetableIssueCode code{NtripSourcetableIssueCode::kUnknownRecord};
  std::string line{};
};

struct NtripSourcetable
{
  std::vector<NtripSourcetableStream> streams{};
  std::vector<NtripSourcetableCaster> casters{};
  std::vector<NtripSourcetableNetwork> networks{};
  std::vector<NtripSourcetableIssue> issues{};
  bool has_end_marker{false};
};

NtripSourcetable ParseNtripSourcetable(std::string_view text);

bool IsRtcmStream(const NtripSourcetableStream& stream);
bool RequiresNmea(const NtripSourcetableStream& stream);
bool SupportsMsm(const NtripSourcetableStream& stream);

const NtripSourcetableStream* FindMountpoint(const NtripSourcetable& sourcetable,
                                             std::string_view mountpoint);
std::vector<const NtripSourcetableStream*> FilterRtcmStreams(const NtripSourcetable& sourcetable);

}  // namespace universal_gnss_ntrip
