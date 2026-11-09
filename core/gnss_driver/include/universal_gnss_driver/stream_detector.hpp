#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace universal_gnss_driver
{

enum class DetectedStreamProtocol : std::uint8_t
{
  kUnknown = 0,
  kNmea = 1,
  kUbx = 2,
  kRtcm3 = 3,
  kUnicoreAscii = 4,
  kUnicoreBinary = 5,
};

struct StreamDetectionResult
{
  DetectedStreamProtocol protocol{DetectedStreamProtocol::kUnknown};
  std::size_t bytes_consumed{0};
  std::size_t frame_length_bytes{0};
};

class StreamDetector
{
public:
  StreamDetectionResult Detect(const std::uint8_t* data, std::size_t size) const;

  StreamDetectionResult Detect(const std::vector<std::uint8_t>& bytes) const;
};

const char* ToString(DetectedStreamProtocol protocol);

}  // namespace universal_gnss_driver
