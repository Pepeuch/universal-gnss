#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/nmea_records.hpp"

namespace universal_gnss_ntrip
{

enum class GgaSentenceTalker : std::uint8_t
{
  kGp = 0,
  kGn = 1,
};

enum class GgaSentenceBuildError : std::uint8_t
{
  kNone = 0,
  kMissingLatitude = 1,
  kMissingLongitude = 2,
  kInvalidLatitude = 3,
  kInvalidLongitude = 4,
  kInvalidUtcTime = 5,
};

struct GgaSentenceBuilderOptions
{
  GgaSentenceTalker talker{GgaSentenceTalker::kGp};
  std::optional<universal_gnss_protocols::NmeaUtcTime> utc_time{};
};

struct GgaSentenceBuildResult
{
  GgaSentenceBuildError error{GgaSentenceBuildError::kNone};
  universal_gnss_protocols::NmeaGgaFixQuality fix_quality{
      universal_gnss_protocols::NmeaGgaFixQuality::kInvalid};
  std::string sentence{};

  bool ok() const;
};

universal_gnss_protocols::NmeaGgaFixQuality MapRuntimeStateToGgaFixQuality(
    const universal_gnss::GnssRuntimeState& state);

GgaSentenceBuildResult BuildNmeaGgaSentence(
    const universal_gnss::GnssRuntimeState& state,
    const GgaSentenceBuilderOptions& options = {});

}  // namespace universal_gnss_ntrip
