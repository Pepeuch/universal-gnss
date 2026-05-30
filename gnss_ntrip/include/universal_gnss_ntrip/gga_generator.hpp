#pragma once

#include <cstdint>
#include <string>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_protocols/nmea_records.hpp"

namespace universal_gnss_ntrip
{

enum class GgaGenerationError : std::uint8_t
{
  kNone = 0,
  kMissingLatitude = 1,
  kMissingLongitude = 2,
  kInvalidLatitude = 3,
  kInvalidLongitude = 4,
};

struct GgaGenerationResult
{
  GgaGenerationError error{GgaGenerationError::kNone};
  universal_gnss_protocols::NmeaGgaFixQuality fix_quality{
      universal_gnss_protocols::NmeaGgaFixQuality::kInvalid};
  std::string sentence{};

  bool ok() const;
};

universal_gnss_protocols::NmeaGgaFixQuality MapRuntimeStateToGgaFixQuality(
    const universal_gnss::GnssRuntimeState& state);

GgaGenerationResult BuildNmeaGgaSentence(const universal_gnss::GnssRuntimeState& state);

}  // namespace universal_gnss_ntrip
