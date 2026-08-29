#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_protocols/unicore_binary_framer.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"
#include "universal_gnss_ros2/msg/rtcm_frame.hpp"
#include <gtest/gtest.h>
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
#include "universal_gnss_ros2/ntrip_node.hpp"
#include <sys/socket.h>
#include <unistd.h>
#endif
#include "universal_gnss_ros2/receiver_node.hpp"
#include "universal_gnss_transport/memory_stream.hpp"

namespace
{

std::vector<std::uint8_t> BuildNmeaSentence(const std::string& payload)
{
  std::vector<std::uint8_t> bytes;
  bytes.push_back(static_cast<std::uint8_t>('$'));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  bytes.push_back(static_cast<std::uint8_t>('*'));

  const std::uint8_t checksum = universal_gnss_protocols::ComputeNmeaChecksum(payload);
  constexpr char kHexDigits[] = "0123456789ABCDEF";
  bytes.push_back(static_cast<std::uint8_t>(kHexDigits[(checksum >> 4u) & 0x0Fu]));
  bytes.push_back(static_cast<std::uint8_t>(kHexDigits[checksum & 0x0Fu]));
  bytes.push_back(static_cast<std::uint8_t>('\r'));
  bytes.push_back(static_cast<std::uint8_t>('\n'));
  return bytes;
}

void AppendBytes(std::vector<std::uint8_t>& destination, const std::vector<std::uint8_t>& source)
{
  destination.insert(destination.end(), source.begin(), source.end());
}

std::vector<std::uint8_t> BuildBytes(const std::string& text)
{
  return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::string BuildUnicoreAsciiFrame(const std::string& frame_without_crc)
{
  const auto crc =
      universal_gnss_protocols::ComputeUnicoreBinaryCrc32(reinterpret_cast<const std::uint8_t*>(
                                                              frame_without_crc.data() + 1u),
                                                          frame_without_crc.size() - 1u);

  std::ostringstream stream;
  stream << frame_without_crc << '*' << std::hex << std::nouppercase << std::setw(8)
         << std::setfill('0') << crc << "\r\n";
  return stream.str();
}

std::string BuildInvalidUnicoreAsciiFrame(const std::string& frame_without_crc)
{
  std::string frame = BuildUnicoreAsciiFrame(frame_without_crc);
  frame[frame.size() - 4u] = frame[frame.size() - 4u] == '0' ? '1' : '0';
  return frame;
}

std::vector<std::uint8_t> BuildRtcmFrame(const std::uint16_t message_type,
                                         const bool valid_crc = true)
{
  const std::vector<std::uint8_t> payload = {
      static_cast<std::uint8_t>((message_type >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((message_type & 0x0Fu) << 4u),
  };

  std::vector<std::uint8_t> bytes = {
      0xD3u,
      0x00u,
      static_cast<std::uint8_t>(payload.size()),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  std::uint32_t crc = universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  if (!valid_crc)
  {
    crc ^= 0x01u;
  }

  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

void AppendBit(std::vector<std::uint8_t>& payload, std::size_t& bit_offset, const bool bit)
{
  if ((bit_offset % 8u) == 0u)
  {
    payload.push_back(0u);
  }

  if (bit)
  {
    payload.back() |= static_cast<std::uint8_t>(1u << (7u - (bit_offset % 8u)));
  }
  ++bit_offset;
}

void AppendUnsignedBits(std::vector<std::uint8_t>& payload,
                        std::size_t& bit_offset,
                        const std::uint64_t value,
                        const std::size_t bit_count)
{
  for (std::size_t i = 0u; i < bit_count; ++i)
  {
    const std::size_t shift = bit_count - 1u - i;
    AppendBit(payload, bit_offset, ((value >> shift) & 0x01u) != 0u);
  }
}

void AppendSignedBits(std::vector<std::uint8_t>& payload,
                      std::size_t& bit_offset,
                      const std::int64_t value,
                      const std::size_t bit_count)
{
  const std::uint64_t mask = (1ULL << bit_count) - 1ULL;
  AppendUnsignedBits(payload, bit_offset, static_cast<std::uint64_t>(value) & mask, bit_count);
}

void AppendZeroBits(std::vector<std::uint8_t>& payload,
                    std::size_t& bit_offset,
                    const std::size_t bit_count)
{
  for (std::size_t index = 0u; index < bit_count; ++index)
  {
    AppendBit(payload, bit_offset, false);
  }
}

std::size_t GetRtcmMsmBodyBits(const std::uint8_t msm_variant,
                               const std::size_t satellite_count,
                               const std::size_t populated_cell_count)
{
  switch (msm_variant)
  {
    case 4u:
      return satellite_count * 18u + populated_cell_count * 48u;
    case 5u:
      return satellite_count * 36u + populated_cell_count * 63u;
    case 6u:
      return satellite_count * 18u + populated_cell_count * 65u;
    case 7u:
      return satellite_count * 36u + populated_cell_count * 80u;
    default:
      return 0u;
  }
}

std::vector<std::uint8_t> BuildRtcmFrameFromPayload(const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> bytes = {
      0xD3u,
      static_cast<std::uint8_t>((payload.size() >> 8u) & 0x03u),
      static_cast<std::uint8_t>(payload.size() & 0xFFu),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
  bytes.push_back(static_cast<std::uint8_t>((crc >> 16u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>((crc >> 8u) & 0xFFu));
  bytes.push_back(static_cast<std::uint8_t>(crc & 0xFFu));
  return bytes;
}

std::vector<std::uint8_t> BuildRtcm1006Frame(const std::uint16_t station_id,
                                             const std::int64_t ecef_x_0_1mm,
                                             const std::int64_t ecef_y_0_1mm,
                                             const std::int64_t ecef_z_0_1mm,
                                             const std::uint16_t antenna_height_0_1mm)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;
  AppendUnsignedBits(payload, bit_offset, 1006u, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, 21u, 6u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendSignedBits(payload, bit_offset, ecef_x_0_1mm, 38u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendSignedBits(payload, bit_offset, ecef_y_0_1mm, 38u);
  AppendUnsignedBits(payload, bit_offset, 1u, 2u);
  AppendSignedBits(payload, bit_offset, ecef_z_0_1mm, 38u);
  AppendUnsignedBits(payload, bit_offset, antenna_height_0_1mm, 16u);
  return BuildRtcmFrameFromPayload(payload);
}

std::vector<std::uint8_t> BuildRtcm1230Frame(const std::uint16_t station_id,
                                             const bool code_phase_bias_indicator,
                                             const bool has_l1_ca_bias,
                                             const bool has_l1_p_bias,
                                             const bool has_l2_ca_bias,
                                             const bool has_l2_p_bias,
                                             const std::optional<std::int16_t> l1_ca_bias_raw,
                                             const std::optional<std::int16_t> l1_p_bias_raw,
                                             const std::optional<std::int16_t> l2_ca_bias_raw,
                                             const std::optional<std::int16_t> l2_p_bias_raw)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;
  AppendUnsignedBits(payload, bit_offset, 1230u, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, code_phase_bias_indicator ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 3u);
  AppendUnsignedBits(payload, bit_offset, has_l1_ca_bias ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, has_l1_p_bias ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, has_l2_ca_bias ? 1u : 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, has_l2_p_bias ? 1u : 0u, 1u);
  if (has_l1_ca_bias)
  {
    AppendSignedBits(payload, bit_offset, *l1_ca_bias_raw, 16u);
  }
  if (has_l1_p_bias)
  {
    AppendSignedBits(payload, bit_offset, *l1_p_bias_raw, 16u);
  }
  if (has_l2_ca_bias)
  {
    AppendSignedBits(payload, bit_offset, *l2_ca_bias_raw, 16u);
  }
  if (has_l2_p_bias)
  {
    AppendSignedBits(payload, bit_offset, *l2_p_bias_raw, 16u);
  }
  return BuildRtcmFrameFromPayload(payload);
}

std::vector<std::uint8_t> BuildRtcmMsmPayload(const std::uint16_t message_type,
                                              const std::uint16_t station_id,
                                              const std::vector<std::uint8_t>& satellite_ids,
                                              const std::vector<std::uint8_t>& signal_ids,
                                              const std::vector<bool>& cell_mask)
{
  std::vector<std::uint8_t> payload;
  std::size_t bit_offset = 0u;

  AppendUnsignedBits(payload, bit_offset, message_type, 12u);
  AppendUnsignedBits(payload, bit_offset, station_id, 12u);
  AppendUnsignedBits(payload, bit_offset, 123456u, 30u);
  AppendUnsignedBits(payload, bit_offset, 0u, 1u);
  AppendUnsignedBits(payload, bit_offset, 0u, 3u);
  AppendUnsignedBits(payload, bit_offset, 15u, 7u);
  AppendUnsignedBits(payload, bit_offset, 1u, 2u);
  AppendUnsignedBits(payload, bit_offset, 0u, 2u);
  AppendUnsignedBits(payload, bit_offset, 1u, 1u);
  AppendUnsignedBits(payload, bit_offset, 3u, 3u);

  for (std::uint8_t satellite = 1u; satellite <= 64u; ++satellite)
  {
    bool present = false;
    for (const auto candidate : satellite_ids)
    {
      if (candidate == satellite)
      {
        present = true;
        break;
      }
    }
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
  }

  for (std::uint8_t signal = 1u; signal <= 32u; ++signal)
  {
    bool present = false;
    for (const auto candidate : signal_ids)
    {
      if (candidate == signal)
      {
        present = true;
        break;
      }
    }
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
  }

  std::size_t populated_cell_count = 0u;
  for (const bool present : cell_mask)
  {
    AppendUnsignedBits(payload, bit_offset, present ? 1u : 0u, 1u);
    if (present)
    {
      ++populated_cell_count;
    }
  }

  AppendZeroBits(payload,
                 bit_offset,
                 GetRtcmMsmBodyBits(universal_gnss_protocols::GetRtcmMsmVariant(message_type),
                                    satellite_ids.size(),
                                    populated_cell_count));
  return payload;
}

std::vector<std::uint8_t> BuildRtcmMsmFrame(const std::uint16_t message_type,
                                            const std::uint16_t station_id,
                                            const std::vector<std::uint8_t>& satellite_ids,
                                            const std::vector<std::uint8_t>& signal_ids,
                                            const std::vector<bool>& cell_mask)
{
  return BuildRtcmFrameFromPayload(
      BuildRtcmMsmPayload(message_type, station_id, satellite_ids, signal_ids, cell_mask));
}

void WriteLeU2(std::vector<std::uint8_t>& payload,
               const std::size_t offset,
               const std::uint16_t value)
{
  payload[offset] = static_cast<std::uint8_t>(value & 0xFFu);
  payload[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

std::vector<std::uint8_t> BuildUbxFrame(const std::uint8_t class_id,
                                        const std::uint8_t message_id,
                                        const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> bytes = {
      0xB5u,
      0x62u,
      class_id,
      message_id,
      static_cast<std::uint8_t>(payload.size() & 0xFFu),
      static_cast<std::uint8_t>((payload.size() >> 8u) & 0xFFu),
  };
  bytes.insert(bytes.end(), payload.begin(), payload.end());

  const auto checksum =
      universal_gnss_protocols::ComputeUbxChecksum(bytes.data() + 2u, bytes.size() - 2u);
  bytes.push_back(checksum.ck_a);
  bytes.push_back(checksum.ck_b);
  return bytes;
}

std::vector<std::uint8_t> MakeUbxRxmRtcmPayload(const std::uint16_t message_type,
                                                const std::uint16_t ref_station_id,
                                                const std::uint8_t flags)
{
  std::vector<std::uint8_t> payload(8u, 0u);
  payload[0u] = 0x02u;
  payload[1u] = flags;
  WriteLeU2(payload, 2u, 0u);
  WriteLeU2(payload, 4u, ref_station_id);
  WriteLeU2(payload, 6u, message_type);
  return payload;
}

class ScriptedByteSource : public universal_gnss_transport::ByteSource
{
public:
  struct Action
  {
    universal_gnss_transport::TransportStatus status{
        universal_gnss_transport::TransportStatus::kOk};
    universal_gnss_transport::TransportError error{universal_gnss_transport::TransportError::kNone};
    std::vector<std::uint8_t> payload{};
    bool keep_open{true};
  };

  explicit ScriptedByteSource(std::vector<Action> actions) : actions_(std::move(actions))
  {
  }

  universal_gnss_transport::ReadResult Read(std::uint8_t* destination,
                                            std::size_t capacity) override
  {
    if (!open_)
    {
      return {0u,
              universal_gnss_transport::TransportStatus::kClosed,
              universal_gnss_transport::TransportError::kClosed};
    }

    if (next_index_ >= actions_.size())
    {
      return {0u,
              universal_gnss_transport::TransportStatus::kOk,
              universal_gnss_transport::TransportError::kNone};
    }

    const Action action = actions_[next_index_++];
    open_ = action.keep_open;

    if (action.status != universal_gnss_transport::TransportStatus::kOk)
    {
      return {0u, action.status, action.error};
    }

    if (destination == nullptr || capacity < action.payload.size())
    {
      return {0u,
              universal_gnss_transport::TransportStatus::kError,
              universal_gnss_transport::TransportError::kInvalidArgument};
    }

    std::copy(action.payload.begin(), action.payload.end(), destination);
    return {action.payload.size(),
            universal_gnss_transport::TransportStatus::kOk,
            universal_gnss_transport::TransportError::kNone};
  }

  bool IsOpen() const override
  {
    return open_;
  }

  void Close() override
  {
    open_ = false;
  }

private:
  std::vector<Action> actions_{};
  std::size_t next_index_{0u};
  bool open_{true};
};

const diagnostic_msgs::msg::DiagnosticStatus* FindDiagnosticStatusByName(
    const diagnostic_msgs::msg::DiagnosticArray& array, const std::string& name)
{
  for (const auto& status : array.status)
  {
    if (status.name == name)
    {
      return &status;
    }
  }
  return nullptr;
}

std::optional<std::string> FindDiagnosticValue(const diagnostic_msgs::msg::DiagnosticStatus& status,
                                               const std::string& key)
{
  for (const auto& entry : status.values)
  {
    if (entry.key == key)
    {
      return entry.value;
    }
  }
  return std::nullopt;
}

std::int64_t RosTimeToNanoseconds(const builtin_interfaces::msg::Time& stamp)
{
  return static_cast<std::int64_t>(stamp.sec) * 1000000000LL +
         static_cast<std::int64_t>(stamp.nanosec);
}

class ReceiverNodeTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (!rclcpp::ok())
    {
      rclcpp::init(0, nullptr);
    }
  }

  static void TearDownTestSuite()
  {
    if (rclcpp::ok())
    {
      rclcpp::shutdown();
    }
  }
};

universal_gnss_driver::ReceiverProbeResult MakeDiscoveryResult(
    const std::string& path,
    const std::uint32_t baud,
    const universal_gnss_driver::ReceiverDetectedFamily family,
    const universal_gnss_driver::ReceiverProbeConfidence confidence)
{
  universal_gnss_driver::ReceiverProbeResult result;
  result.path = path;
  result.source = universal_gnss_driver::ReceiverPortSource::kExplicitPath;
  result.transport_type = universal_gnss_driver::ReceiverTransportType::kSerial;
  result.selected_baud = baud;
  result.detected_family = family;
  result.confidence = confidence;
  result.discovery_score = confidence == universal_gnss_driver::ReceiverProbeConfidence::kHigh ? 100
                           : confidence == universal_gnss_driver::ReceiverProbeConfidence::kMedium
                               ? 20
                           : confidence == universal_gnss_driver::ReceiverProbeConfidence::kLow ? 10
                                                                                                : 0;
  result.evidence.bytes_read = 128u;
  if (family == universal_gnss_driver::ReceiverDetectedFamily::kUblox)
  {
    result.evidence.ubx_frames_seen = 1u;
    result.reason = "valid_ubx_frame:+100";
  }
  else if (family == universal_gnss_driver::ReceiverDetectedFamily::kUnicore)
  {
    result.evidence.unicore_binary_seen = 1u;
    result.reason = "unicore_binary:+100";
  }
  else if (family == universal_gnss_driver::ReceiverDetectedFamily::kNmea)
  {
    result.evidence.nmea_sentences_seen = 1u;
    result.reason = "valid_GGA:+20";
  }
  return result;
}

TEST_F(ReceiverNodeTest, ConstructsWithParametersAndPublishersReady)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("receiver_family", "unicore"),
      rclcpp::Parameter("transport", "tcp"),
      rclcpp::Parameter("tcp_host", "127.0.0.1"),
      rclcpp::Parameter("tcp_port", 2101),
      rclcpp::Parameter("publish_rate_hz", 5.0),
      rclcpp::Parameter("frame_id", "base_link"),
  });

  auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>();
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.publishers_ready());
  EXPECT_TRUE(node.has_transport_source());
  EXPECT_EQ(node.get_parameter("receiver_family").as_string(), "unicore");
  EXPECT_EQ(node.get_parameter("transport").as_string(), "tcp");
  EXPECT_EQ(node.get_parameter("tcp_host").as_string(), "127.0.0.1");
  EXPECT_EQ(node.get_parameter("tcp_port").as_int(), 2101);
  EXPECT_DOUBLE_EQ(node.get_parameter("publish_rate_hz").as_double(), 5.0);
  EXPECT_EQ(node.get_parameter("frame_id").as_string(), "base_link");
}

TEST_F(ReceiverNodeTest, ExplicitSerialConfigDoesNotRunDiscovery)
{
  bool discovery_called = false;
  auto discovery = [&](const universal_gnss_driver::ReceiverProbeConfig&,
                       const std::optional<std::string>&,
                       const universal_gnss_driver::ReceiverDiscoveryPaths&)
  {
    discovery_called = true;
    return std::vector<universal_gnss_driver::ReceiverProbeResult>{};
  };

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("transport", "serial"),
      rclcpp::Parameter("serial_device", "/definitely_missing_universal_gnss_device"),
      rclcpp::Parameter("serial_baud", 921600),
      rclcpp::Parameter("receiver_family", "ublox"),
  });

  universal_gnss_ros2::ReceiverNode node(discovery, options);
  EXPECT_FALSE(discovery_called);
  EXPECT_FALSE(node.has_transport_source());
}

TEST_F(ReceiverNodeTest, AutoDiscoveryChoosesHighConfidenceUbloxResult)
{
  std::optional<std::string> captured_path;
  bool captured_include_platform = false;
  auto discovery = [&](const universal_gnss_driver::ReceiverProbeConfig& config,
                       const std::optional<std::string>& explicit_path,
                       const universal_gnss_driver::ReceiverDiscoveryPaths&)
  {
    captured_path = explicit_path;
    captured_include_platform = config.include_platform_uarts;
    return std::vector<universal_gnss_driver::ReceiverProbeResult>{
        MakeDiscoveryResult("/dev/serial/by-id/f9p",
                            921600u,
                            universal_gnss_driver::ReceiverDetectedFamily::kUblox,
                            universal_gnss_driver::ReceiverProbeConfidence::kHigh)};
  };

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("transport", "serial"),
      rclcpp::Parameter("serial_device", "auto"),
      rclcpp::Parameter("serial_baud", std::string("auto")),
      rclcpp::Parameter("receiver_family", "auto"),
      rclcpp::Parameter("discovery_include_platform_uarts", true),
  });

  universal_gnss_ros2::ReceiverNode node(discovery, options);
  EXPECT_FALSE(captured_path.has_value());
  EXPECT_TRUE(captured_include_platform);
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* discovery_status =
      FindDiagnosticStatusByName(*node.last_diagnostics_message(), "universal_gnss/discovery");
  ASSERT_NE(discovery_status, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "attempted"),
            std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "succeeded"),
            std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "path"),
            std::optional<std::string>{"/dev/serial/by-id/f9p"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "baud"), std::optional<std::string>{"921600"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "family"), std::optional<std::string>{"ublox"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "confidence"),
            std::optional<std::string>{"high"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "detected_family"),
            std::optional<std::string>{"ublox"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "detected_device"),
            std::optional<std::string>{"/dev/serial/by-id/f9p"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "detected_baud"),
            std::optional<std::string>{"921600"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "discovery_confidence"),
            std::optional<std::string>{"100"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "discovery_reason"),
            std::optional<std::string>{"valid_ubx_frame:+100"});
}

TEST_F(ReceiverNodeTest, ExplicitPathWithAutoBaudAndFamilyProbesOnlyThatPath)
{
  std::optional<std::string> captured_path;
  std::vector<std::uint32_t> captured_bauds;
  auto discovery = [&](const universal_gnss_driver::ReceiverProbeConfig& config,
                       const std::optional<std::string>& explicit_path,
                       const universal_gnss_driver::ReceiverDiscoveryPaths&)
  {
    captured_path = explicit_path;
    captured_bauds = config.baud_candidates;
    return std::vector<universal_gnss_driver::ReceiverProbeResult>{
        MakeDiscoveryResult("/dev/ttyAMA2",
                            921600u,
                            universal_gnss_driver::ReceiverDetectedFamily::kUnicore,
                            universal_gnss_driver::ReceiverProbeConfidence::kHigh)};
  };

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("transport", "serial"),
      rclcpp::Parameter("serial_device", "/dev/ttyAMA2"),
      rclcpp::Parameter("serial_baud", std::string("auto")),
      rclcpp::Parameter("receiver_family", "auto"),
  });

  universal_gnss_ros2::ReceiverNode node(discovery, options);
  ASSERT_TRUE(captured_path.has_value());
  EXPECT_EQ(*captured_path, "/dev/ttyAMA2");
  ASSERT_FALSE(captured_bauds.empty());
  EXPECT_EQ(captured_bauds.front(), 921600u);
}

TEST_F(ReceiverNodeTest, DiscoveryFailureIsReportedClearly)
{
  auto discovery = [&](const universal_gnss_driver::ReceiverProbeConfig&,
                       const std::optional<std::string>&,
                       const universal_gnss_driver::ReceiverDiscoveryPaths&)
  {
    return std::vector<universal_gnss_driver::ReceiverProbeResult>{};
  };

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("transport", "serial"),
      rclcpp::Parameter("serial_device", "auto"),
      rclcpp::Parameter("serial_baud", std::string("auto")),
      rclcpp::Parameter("receiver_family", "auto"),
  });

  universal_gnss_ros2::ReceiverNode node(discovery, options);
  EXPECT_FALSE(node.has_transport_source());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* discovery_status =
      FindDiagnosticStatusByName(*node.last_diagnostics_message(), "universal_gnss/discovery");
  ASSERT_NE(discovery_status, nullptr);
  EXPECT_EQ(discovery_status->level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "succeeded"),
            std::optional<std::string>{"false"});
  EXPECT_TRUE(FindDiagnosticValue(*discovery_status, "failure_reason").has_value());
}

TEST_F(ReceiverNodeTest, LowConfidenceDiscoveryIsRejected)
{
  auto discovery = [&](const universal_gnss_driver::ReceiverProbeConfig&,
                       const std::optional<std::string>&,
                       const universal_gnss_driver::ReceiverDiscoveryPaths&)
  {
    return std::vector<universal_gnss_driver::ReceiverProbeResult>{
        MakeDiscoveryResult("/dev/ttyS1",
                            921600u,
                            universal_gnss_driver::ReceiverDetectedFamily::kUnknown,
                            universal_gnss_driver::ReceiverProbeConfidence::kLow)};
  };

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("transport", "serial"),
      rclcpp::Parameter("serial_device", "auto"),
      rclcpp::Parameter("serial_baud", std::string("auto")),
      rclcpp::Parameter("receiver_family", "auto"),
  });

  universal_gnss_ros2::ReceiverNode node(discovery, options);
  EXPECT_FALSE(node.has_transport_source());
}

TEST_F(ReceiverNodeTest, GenericNmeaDiscoveryRequiresExplicitOptIn)
{
  auto make_discovery = []()
  {
    return [](const universal_gnss_driver::ReceiverProbeConfig&,
              const std::optional<std::string>&,
              const universal_gnss_driver::ReceiverDiscoveryPaths&)
    {
      return std::vector<universal_gnss_driver::ReceiverProbeResult>{
          MakeDiscoveryResult("/dev/ttyAMA2",
                              921600u,
                              universal_gnss_driver::ReceiverDetectedFamily::kNmea,
                              universal_gnss_driver::ReceiverProbeConfidence::kMedium)};
    };
  };

  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(std::vector<rclcpp::Parameter>{
        rclcpp::Parameter("transport", "serial"),
        rclcpp::Parameter("serial_device", "auto"),
        rclcpp::Parameter("serial_baud", std::string("auto")),
        rclcpp::Parameter("receiver_family", "auto"),
        rclcpp::Parameter("discovery_allow_generic_nmea", false),
    });

    universal_gnss_ros2::ReceiverNode node(make_discovery(), options);
    EXPECT_FALSE(node.has_transport_source());
    node.PublishNow();
    ASSERT_TRUE(node.last_diagnostics_message().has_value());
    const auto* discovery_status =
        FindDiagnosticStatusByName(*node.last_diagnostics_message(), "universal_gnss/discovery");
    ASSERT_NE(discovery_status, nullptr);
    EXPECT_EQ(discovery_status->level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  }

  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(std::vector<rclcpp::Parameter>{
        rclcpp::Parameter("transport", "serial"),
        rclcpp::Parameter("serial_device", "auto"),
        rclcpp::Parameter("serial_baud", std::string("auto")),
        rclcpp::Parameter("receiver_family", "auto"),
        rclcpp::Parameter("discovery_allow_generic_nmea", true),
    });

    universal_gnss_ros2::ReceiverNode node(make_discovery(), options);
    node.PublishNow();
    ASSERT_TRUE(node.last_diagnostics_message().has_value());
    const auto* discovery_status =
        FindDiagnosticStatusByName(*node.last_diagnostics_message(), "universal_gnss/discovery");
    ASSERT_NE(discovery_status, nullptr);
    EXPECT_EQ(discovery_status->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
    EXPECT_EQ(FindDiagnosticValue(*discovery_status, "family"), std::optional<std::string>{"nmea"});
  }
}

TEST_F(ReceiverNodeTest, DiscoveryReceivesPlatformUartOptInAndKnownBaud)
{
  bool include_platform_uarts = false;
  std::vector<std::uint32_t> captured_bauds;
  auto discovery = [&](const universal_gnss_driver::ReceiverProbeConfig& config,
                       const std::optional<std::string>& explicit_path,
                       const universal_gnss_driver::ReceiverDiscoveryPaths&)
  {
    include_platform_uarts = config.include_platform_uarts;
    captured_bauds = config.baud_candidates;
    EXPECT_EQ(explicit_path, std::optional<std::string>{"/dev/ttyAMA2"});
    return std::vector<universal_gnss_driver::ReceiverProbeResult>{
        MakeDiscoveryResult("/dev/ttyAMA2",
                            921600u,
                            universal_gnss_driver::ReceiverDetectedFamily::kUnicore,
                            universal_gnss_driver::ReceiverProbeConfidence::kHigh)};
  };

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("transport", "serial"),
      rclcpp::Parameter("serial_device", "/dev/ttyAMA2"),
      rclcpp::Parameter("serial_baud", 921600),
      rclcpp::Parameter("receiver_family", "auto"),
      rclcpp::Parameter("discovery_include_platform_uarts", true),
  });

  universal_gnss_ros2::ReceiverNode node(discovery, options);
  EXPECT_TRUE(include_platform_uarts);
  ASSERT_EQ(captured_bauds.size(), 1u);
  EXPECT_EQ(captured_bauds.front(), 921600u);
}

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
TEST_F(ReceiverNodeTest, ReceiverAndNtripNodesConstructWithCompatibleParameters)
{
  rclcpp::NodeOptions receiver_options;
  receiver_options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("receiver_family", "ublox"),
      rclcpp::Parameter("frame_id", "gnss_link"),
  });

  rclcpp::NodeOptions ntrip_options;
  ntrip_options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("caster_host", "caster.example.com"),
      rclcpp::Parameter("caster_port", 2101),
      rclcpp::Parameter("mountpoint", "RTCM3"),
      rclcpp::Parameter("gga_enabled", true),
  });

  auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>();
  universal_gnss_ros2::ReceiverNode receiver(std::move(source), receiver_options);
  universal_gnss_ros2::NtripNode ntrip(ntrip_options);

  EXPECT_TRUE(receiver.publishers_ready());
  EXPECT_TRUE(ntrip.diagnostics_ready());
}

TEST_F(ReceiverNodeTest, ReceiverConsumesRtcmPublishedByNtripNode)
{
  class SocketPair
  {
  public:
    ~SocketPair()
    {
      if (peer_fd_ >= 0)
      {
        ::close(peer_fd_);
      }
      if (client_fd_ >= 0)
      {
        ::close(client_fd_);
      }
    }

    bool Open()
    {
      int fds[2] = {-1, -1};
      if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
      {
        return false;
      }
      client_fd_ = fds[0];
      peer_fd_ = fds[1];
      return true;
    }

    int ReleaseClientFd()
    {
      const int fd = client_fd_;
      client_fd_ = -1;
      return fd;
    }

    bool WritePeer(const std::string& text)
    {
      return WritePeer(std::vector<std::uint8_t>(text.begin(), text.end()));
    }

    bool WritePeer(const std::vector<std::uint8_t>& data)
    {
      std::size_t offset = 0u;
      while (offset < data.size())
      {
        const ssize_t bytes_written = ::write(peer_fd_,
                                              data.data() + static_cast<std::ptrdiff_t>(offset),
                                              data.size() - offset);
        if (bytes_written < 0)
        {
          if (errno == EINTR)
          {
            continue;
          }
          return false;
        }
        offset += static_cast<std::size_t>(bytes_written);
      }
      return true;
    }

    std::string ReadPeerText(const std::size_t max_bytes)
    {
      std::vector<char> buffer(max_bytes, '\0');
      const ssize_t bytes_read = ::recv(peer_fd_, buffer.data(), buffer.size(), MSG_DONTWAIT);
      if (bytes_read <= 0)
      {
        return {};
      }
      return std::string(buffer.data(), buffer.data() + bytes_read);
    }

  private:
    int client_fd_{-1};
    int peer_fd_{-1};
  };

  SocketPair sockets;
  ASSERT_TRUE(sockets.Open());

  rclcpp::NodeOptions receiver_options;
  receiver_options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "ublox")});

  rclcpp::NodeOptions ntrip_options;
  ntrip_options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("caster_host", "caster.example.com"),
      rclcpp::Parameter("caster_port", 2101),
      rclcpp::Parameter("mountpoint", "RTCM3"),
      rclcpp::Parameter("gga_enabled", false),
  });

  auto duplex = std::make_unique<universal_gnss_transport::MemoryByteDuplex>();
  auto* duplex_ptr = duplex.get();
  universal_gnss_ros2::ReceiverNode receiver(std::move(duplex), receiver_options);
  universal_gnss_ros2::NtripNode ntrip(sockets.ReleaseClientFd(), ntrip_options);
  auto bridge_node = std::make_shared<rclcpp::Node>("receiver_rtcm_bridge");
  auto bridge_publisher =
      bridge_node->create_publisher<universal_gnss_ros2::msg::RtcmFrame>("rtcm", 10);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(receiver.get_node_base_interface());
  executor.add_node(ntrip.get_node_base_interface());
  executor.add_node(bridge_node->get_node_base_interface());
  executor.spin_some();

  EXPECT_TRUE(ntrip.StepOnce());
  EXPECT_NE(sockets.ReadPeerText(1024u).find("GET /RTCM3 HTTP/1.1"), std::string::npos);

  const auto rtcm = BuildRtcmFrame(1077u);
  ASSERT_TRUE(sockets.WritePeer("ICY 200 OK\r\n"));
  ASSERT_TRUE(sockets.WritePeer(rtcm));
  for (std::size_t attempt = 0u; attempt < 8u && !ntrip.last_rtcm_message().has_value(); ++attempt)
  {
    ntrip.StepOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(ntrip.last_rtcm_message().has_value());

  for (std::size_t attempt = 0u; attempt < 50u && bridge_publisher->get_subscription_count() == 0u;
       ++attempt)
  {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_GT(bridge_publisher->get_subscription_count(), 0u);

  bridge_publisher->publish(*ntrip.last_rtcm_message());
  for (std::size_t attempt = 0u; attempt < 8u && duplex_ptr->written_bytes().empty(); ++attempt)
  {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EXPECT_EQ(duplex_ptr->written_bytes(), rtcm);

  receiver.PublishNow();
  ASSERT_TRUE(receiver.last_diagnostics_message().has_value());
  const auto* forwarding = FindDiagnosticStatusByName(*receiver.last_diagnostics_message(),
                                                      "universal_gnss/rtcm_forwarding");
  ASSERT_NE(forwarding, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "forwarded_frame_count"),
            std::optional<std::string>{"1"});
}
#endif

TEST_F(ReceiverNodeTest, ProjectsRuntimeUpdatesThroughRosAdapters)
{
  std::vector<std::uint8_t> stream;
  AppendBytes(stream,
              BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
  AppendBytes(stream, BuildNmeaSentence("GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5"));
  AppendBytes(stream,
              BuildNmeaSentence(
                  "GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"));
  AppendBytes(stream, BuildNmeaSentence("GPGST,123519.00,1.2,0.8,0.7,45.0,0.5,0.6,1.1"));

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("receiver_family", "nmea"),
      rclcpp::Parameter("frame_id", "gnss"),
  });

  auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>(std::move(stream));
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_fix_message().has_value());
  ASSERT_TRUE(node.last_status_message().has_value());
  ASSERT_TRUE(node.last_diagnostics_message().has_value());

  const auto& fix = *node.last_fix_message();
  const auto& status = *node.last_status_message();
  const auto& diagnostics = *node.last_diagnostics_message();

  EXPECT_EQ(fix.header.frame_id, "gnss");
  EXPECT_TRUE(fix.header.stamp.sec != 0 || fix.header.stamp.nanosec != 0u);
  EXPECT_TRUE(status.stamp.sec != 0 || status.stamp.nanosec != 0u);
  EXPECT_TRUE(status.fix_valid);
  EXPECT_EQ(status.fix_type, universal_gnss_ros2::msg::GnssStatus::FIX_TYPE_FIX);
  EXPECT_NEAR(fix.latitude, 48.1173, 1e-4);
  EXPECT_NEAR(fix.longitude, 11.5166667, 1e-4);
  EXPECT_DOUBLE_EQ(fix.altitude, 545.4);
  EXPECT_FLOAT_EQ(status.hdop, 1.0f);
  EXPECT_FLOAT_EQ(status.vdop, 1.5f);
  EXPECT_FLOAT_EQ(status.horizontal_accuracy_m, 0.6f);
  EXPECT_FLOAT_EQ(status.vertical_accuracy_m, 1.1f);
  EXPECT_EQ(status.satellites_used, 8u);
  EXPECT_EQ(status.satellites_visible, 8u);
  EXPECT_FLOAT_EQ(status.max_cn0_db_hz, 43.0f);
  EXPECT_FALSE(diagnostics.status.empty());
  EXPECT_TRUE(diagnostics.header.stamp.sec != 0 || diagnostics.header.stamp.nanosec != 0u);
  EXPECT_EQ(diagnostics.header.frame_id, "gnss");
}

TEST_F(ReceiverNodeTest, PublishesStableReceiptProvenanceInsteadOfPublicationTime)
{
  const auto gga =
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       gga,
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       gga,
       true},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "nmea")});
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  node.PublishNow();
  ASSERT_TRUE(node.last_status_message().has_value());
  EXPECT_EQ(RosTimeToNanoseconds(node.last_status_message()->stamp), 0)
      << "publication time must not be substituted before an observation is accepted";
  EXPECT_EQ(node.last_status_message()->position_observation_sequence, 0u);

  const auto first_receipt_lower_ns = node.now().nanoseconds();
  ASSERT_TRUE(node.StepOnce());
  const auto first_receipt_upper_ns = node.now().nanoseconds();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  node.PublishNow();
  ASSERT_TRUE(node.last_status_message().has_value());
  const auto first_published_stamp_ns =
      RosTimeToNanoseconds(node.last_status_message()->stamp);
  const auto first_position_sequence =
      node.last_status_message()->position_observation_sequence;
  EXPECT_GE(first_published_stamp_ns, first_receipt_lower_ns);
  EXPECT_LE(first_published_stamp_ns, first_receipt_upper_ns);
  EXPECT_EQ(first_position_sequence, 1u);

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  node.PublishNow();
  ASSERT_TRUE(node.last_status_message().has_value());
  EXPECT_EQ(RosTimeToNanoseconds(node.last_status_message()->stamp),
            first_published_stamp_ns)
      << "republishing cached state must preserve its original receipt provenance";
  EXPECT_EQ(node.last_status_message()->position_observation_sequence,
            first_position_sequence)
      << "republishing cached state must not invent a position observation";

  ASSERT_TRUE(node.StepOnce());
  node.PublishNow();
  ASSERT_TRUE(node.last_status_message().has_value());
  EXPECT_GT(RosTimeToNanoseconds(node.last_status_message()->stamp),
            first_published_stamp_ns)
      << "a genuinely new observation must carry new receipt provenance";
  EXPECT_EQ(node.last_status_message()->position_observation_sequence,
            first_position_sequence + 1u)
      << "an identical newly received fix must advance position provenance";
}

TEST_F(ReceiverNodeTest, ProjectsGenericNmeaRtkModeFromGgaFixQuality)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "nmea")});

  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,4,08,0.9,545.4,M,46.9,M,,"),
       true},
  });

  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_status_message().has_value());
  const auto& status = *node.last_status_message();
  EXPECT_TRUE(status.fix_valid);
  EXPECT_EQ(status.fix_type, universal_gnss_ros2::msg::GnssStatus::FIX_TYPE_FIX);
  EXPECT_EQ(status.rtk_mode, universal_gnss_ros2::msg::GnssStatus::RTK_MODE_FIXED);
  EXPECT_NE(status.capability_flags & universal_gnss_ros2::msg::GnssStatus::CAP_RTK_MODE, 0u);
  EXPECT_NE(status.value_flags & universal_gnss_ros2::msg::GnssStatus::CAP_RTK_MODE, 0u);
}

TEST_F(ReceiverNodeTest, PublishesHighPrecisionFixCoordinatesWithoutTruncation)
{
  constexpr double expected_latitude = 48.0 + 7.0381234 / 60.0;
  constexpr double expected_longitude = 11.0 + 31.0005678 / 60.0;

  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "nmea")});

  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildNmeaSentence("GPGGA,123519,4807.0381234,N,01131.0005678,E,1,08,0.9,545.4,M,46.9,M,,"),
       true},
  });

  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_fix_message().has_value());
  ASSERT_TRUE(node.current_state().latitude_deg.has_value());
  ASSERT_TRUE(node.current_state().longitude_deg.has_value());

  const auto& fix = *node.last_fix_message();
  EXPECT_NEAR(fix.latitude, expected_latitude, 1e-12);
  EXPECT_NEAR(fix.longitude, expected_longitude, 1e-12);
  EXPECT_NEAR(fix.latitude, *node.current_state().latitude_deg, 1e-12);
  EXPECT_NEAR(fix.longitude, *node.current_state().longitude_deg, 1e-12);
}

TEST_F(ReceiverNodeTest, SemanticOnlyVtgAndZdaDoNotInventRuntimeFields)
{
  std::vector<std::uint8_t> stream;
  AppendBytes(stream, BuildNmeaSentence("GPVTG,054.7,T,034.4,M,005.5,N,010.2,K,A"));
  AppendBytes(stream, BuildNmeaSentence("GPZDA,201530.00,04,07,2002,00,00"));

  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "nmea")});

  auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>(std::move(stream));
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_status_message().has_value());
  const auto& status = *node.last_status_message();

  EXPECT_FALSE(status.fix_valid);
  EXPECT_EQ(status.fix_type, universal_gnss_ros2::msg::GnssStatus::FIX_TYPE_UNKNOWN);
  EXPECT_FALSE(node.last_fix_message().has_value());
  EXPECT_TRUE(std::isnan(status.latitude_deg));
  EXPECT_TRUE(std::isnan(status.heading_deg));
  EXPECT_EQ(status.capability_flags & universal_gnss_ros2::msg::GnssStatus::CAP_HEADING, 0u);
}

TEST_F(ReceiverNodeTest, RejectsInvalidReceiverFamily)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "mystery")});

  auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>();
  EXPECT_THROW(universal_gnss_ros2::ReceiverNode(std::move(source), options),
               std::invalid_argument);
}

TEST_F(ReceiverNodeTest, RejectsInvalidPublishRateAndFrameId)
{
  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(
        std::vector<rclcpp::Parameter>{rclcpp::Parameter("publish_rate_hz", 0.0)});
    auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>();
    EXPECT_THROW(universal_gnss_ros2::ReceiverNode(std::move(source), options),
                 std::invalid_argument);
  }

  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(std::vector<rclcpp::Parameter>{rclcpp::Parameter("frame_id", "")});
    auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>();
    EXPECT_THROW(universal_gnss_ros2::ReceiverNode(std::move(source), options),
                 std::invalid_argument);
  }
}

TEST_F(ReceiverNodeTest, ValidatesReadChunkSizeBeforeConversionToSizeT)
{
  constexpr std::int64_t kMaximumReadChunkSize = 1024 * 1024;

  for (const std::int64_t value : {
           std::int64_t{-1},
           std::numeric_limits<std::int64_t>::min(),
           std::int64_t{0},
           kMaximumReadChunkSize + 1,
           std::numeric_limits<std::int64_t>::max(),
       })
  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(
        std::vector<rclcpp::Parameter>{rclcpp::Parameter("read_chunk_size", value)});
    auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>();
    EXPECT_THROW(universal_gnss_ros2::ReceiverNode(std::move(source), options),
                 std::invalid_argument)
        << "read_chunk_size=" << value << " should be rejected before conversion to size_t";
  }

  for (const std::int64_t value : {
           std::int64_t{1},
           std::int64_t{65536},
           kMaximumReadChunkSize,
       })
  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(
        std::vector<rclcpp::Parameter>{rclcpp::Parameter("read_chunk_size", value)});
    auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>();
    EXPECT_NO_THROW(universal_gnss_ros2::ReceiverNode(std::move(source), options))
        << "read_chunk_size=" << value << " should remain valid";
  }
}

TEST_F(ReceiverNodeTest, ReportsNoDataReceivedAfterGracePeriod)
{
  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{{}});
  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("expected_runtime_observation_rate_hz", 1.0),
  });
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_FALSE(node.StepOnce());
  std::this_thread::sleep_for(std::chrono::milliseconds(3100));
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* summary = FindDiagnosticStatusByName(diagnostics, "universal_gnss/summary");
  const auto* no_data = FindDiagnosticStatusByName(diagnostics, "universal_gnss/no_data_received");

  ASSERT_NE(summary, nullptr);
  ASSERT_NE(no_data, nullptr);
  EXPECT_EQ(no_data->level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(FindDiagnosticValue(*summary, "transport_healthy"),
            std::optional<std::string>{"false"});
  EXPECT_EQ(FindDiagnosticValue(*summary, "stale_data"), std::optional<std::string>{"false"});
}

TEST_F(ReceiverNodeTest, WindowsParserHealthInsteadOfLatchingLifetimeMalformedCount)
{
  const std::string malformed_line = BuildInvalidUnicoreAsciiFrame(
      "#BESTNAVA,97,GPS,FINE,1,2,0,0,18,16;SOL_COMPUTED,SINGLE,1,2,3");

  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(malformed_line),
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(malformed_line),
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(malformed_line),
       true},
      {universal_gnss_transport::TransportStatus::kEndOfStream,
       universal_gnss_transport::TransportError::kNone,
       {},
       false},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "unicore")});

  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  EXPECT_TRUE(node.StepOnce());
  EXPECT_TRUE(node.StepOnce());
  EXPECT_FALSE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& initial_diagnostics = *node.last_diagnostics_message();
  const auto* initial_summary =
      FindDiagnosticStatusByName(initial_diagnostics, "universal_gnss/summary");
  const auto* initial_malformed =
      FindDiagnosticStatusByName(initial_diagnostics, "universal_gnss/malformed_records");

  ASSERT_NE(initial_summary, nullptr);
  ASSERT_NE(initial_malformed, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*initial_summary, "parser_healthy"),
            std::optional<std::string>{"false"});
  const auto* initial_parser_counters =
      FindDiagnosticStatusByName(initial_diagnostics, "universal_gnss/parser_counters");
  ASSERT_NE(initial_parser_counters, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*initial_parser_counters, "recent_parser_anomalies"),
            std::optional<std::string>{"3"});

  std::this_thread::sleep_for(std::chrono::milliseconds(3100));
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& recovered_diagnostics = *node.last_diagnostics_message();
  const auto* recovered_summary =
      FindDiagnosticStatusByName(recovered_diagnostics, "universal_gnss/summary");
  const auto* recovered_malformed =
      FindDiagnosticStatusByName(recovered_diagnostics, "universal_gnss/malformed_records");

  ASSERT_NE(recovered_summary, nullptr);
  ASSERT_NE(recovered_malformed, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*recovered_summary, "parser_healthy"),
            std::optional<std::string>{"true"});
  EXPECT_EQ(recovered_malformed->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(recovered_malformed->message, "Diagnostic condition cleared");
}

TEST_F(ReceiverNodeTest, IgnoresUnknownButValidUnicoreRecordsForParserHealth)
{
  const std::string unknown_line =
      BuildUnicoreAsciiFrame("#FOOBARA,97,GPS,FINE,1,2,0,0,0,0;payload");

  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(unknown_line),
       true},
      {universal_gnss_transport::TransportStatus::kEndOfStream,
       universal_gnss_transport::TransportError::kNone,
       {},
       false},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "unicore")});

  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  EXPECT_FALSE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* summary = FindDiagnosticStatusByName(diagnostics, "universal_gnss/summary");
  const auto* parser_counters =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/parser_counters");

  ASSERT_NE(summary, nullptr);
  ASSERT_NE(parser_counters, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*summary, "parser_healthy"), std::optional<std::string>{"true"});
  EXPECT_EQ(parser_counters->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(FindDiagnosticValue(*parser_counters, "unknown_records_total"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*parser_counters, "parser_anomalies_total"),
            std::optional<std::string>{"0"});
}

TEST_F(ReceiverNodeTest, PublishesRuntimeStaleRecoveryStatusWhenFreshObservationsResume)
{
  const std::string best_nav = BuildUnicoreAsciiFrame(
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,"
      "1.1053,2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
      "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123");
  const std::string unknown_rtk_status = BuildUnicoreAsciiFrame(
      "#RTKSTATUSA,97,GPS,FINE,2190,365354000,0,0,18,1;"
      "0,0,0,0,0,0,0,0,0,0,0,UNKNOWN,5,0,99,12,0");

  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(best_nav),
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(unknown_rtk_status),
       true},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "unicore"),
                                     rclcpp::Parameter("expected_runtime_observation_rate_hz", 1.0)});

  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  std::this_thread::sleep_for(std::chrono::milliseconds(3100));
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& stale_diagnostics = *node.last_diagnostics_message();
  const auto* stale =
      FindDiagnosticStatusByName(stale_diagnostics, "universal_gnss/runtime_state_stale");
  ASSERT_NE(stale, nullptr);
  EXPECT_EQ(stale->level, diagnostic_msgs::msg::DiagnosticStatus::STALE);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& recovered_diagnostics = *node.last_diagnostics_message();
  const auto* recovered_summary =
      FindDiagnosticStatusByName(recovered_diagnostics, "universal_gnss/summary");
  const auto* recovered_stale =
      FindDiagnosticStatusByName(recovered_diagnostics, "universal_gnss/runtime_state_stale");

  ASSERT_NE(recovered_summary, nullptr);
  ASSERT_NE(recovered_stale, nullptr);
  EXPECT_EQ(recovered_stale->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(recovered_stale->message, "Diagnostic condition cleared");
  EXPECT_EQ(FindDiagnosticValue(*recovered_summary, "stale_data"),
            std::optional<std::string>{"false"});
}

TEST_F(ReceiverNodeTest, KeepsQuarterHertzRuntimeFreshAcrossFourSecondCadence)
{
  const auto gga =
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       gga,
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       gga,
       true},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("receiver_family", "nmea"),
      rclcpp::Parameter("publish_rate_hz", 20.0),
      rclcpp::Parameter("expected_runtime_observation_rate_hz", 0.25),
  });
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();
  ASSERT_TRUE(node.last_fix_message().has_value());

  std::this_thread::sleep_for(std::chrono::milliseconds(3100));
  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& before_next_observation = *node.last_diagnostics_message();
  EXPECT_EQ(FindDiagnosticStatusByName(before_next_observation,
                                       "universal_gnss/runtime_state_stale"),
            nullptr);
  EXPECT_EQ(FindDiagnosticStatusByName(before_next_observation,
                                       "universal_gnss/transport_data_stale"),
            nullptr);
  EXPECT_TRUE(node.last_fix_message().has_value());

  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();
  EXPECT_TRUE(node.last_fix_message().has_value());
}

TEST_F(ReceiverNodeTest, UsesExpectedOneHertzCadenceWithJitter)
{
  const auto gga =
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       gga,
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       gga,
       true},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("receiver_family", "nmea"),
      rclcpp::Parameter("expected_runtime_observation_rate_hz", 1.0),
  });
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  EXPECT_EQ(FindDiagnosticStatusByName(*node.last_diagnostics_message(),
                                       "universal_gnss/runtime_state_stale"),
            nullptr);
  EXPECT_TRUE(node.last_fix_message().has_value());
}

TEST_F(ReceiverNodeTest, DetectsHighRateSilenceAndRecoversAtDerivedTimeout)
{
  const auto gga =
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       gga,
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       gga,
       true},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("receiver_family", "nmea"),
      rclcpp::Parameter("expected_runtime_observation_rate_hz", 10.0),
  });
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  // The derived timeout is three 10 Hz periods (300 ms). Check immediately
  // before and after that boundary rather than relying on ROS publish cadence.
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  EXPECT_EQ(FindDiagnosticStatusByName(*node.last_diagnostics_message(),
                                       "universal_gnss/runtime_state_stale"),
            nullptr);
  EXPECT_TRUE(node.last_fix_message().has_value());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* stale = FindDiagnosticStatusByName(*node.last_diagnostics_message(),
                                                 "universal_gnss/runtime_state_stale");
  ASSERT_NE(stale, nullptr);
  EXPECT_EQ(stale->level, diagnostic_msgs::msg::DiagnosticStatus::STALE);
  EXPECT_FALSE(node.last_fix_message().has_value());

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* recovered = FindDiagnosticStatusByName(*node.last_diagnostics_message(),
                                                     "universal_gnss/runtime_state_stale");
  ASSERT_NE(recovered, nullptr);
  EXPECT_EQ(recovered->message, "Diagnostic condition cleared");
  EXPECT_TRUE(node.last_fix_message().has_value());
}

TEST_F(ReceiverNodeTest, UsesConservativeFallbackWithoutExpectedCadence)
{
  const auto gga =
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,");
  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       gga,
       true},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("receiver_family", "nmea"),
      rclcpp::Parameter("runtime_observation_fallback_timeout_s", 0.05),
  });
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* stale = FindDiagnosticStatusByName(*node.last_diagnostics_message(),
                                                 "universal_gnss/runtime_state_stale");
  ASSERT_NE(stale, nullptr);
  EXPECT_EQ(stale->level, diagnostic_msgs::msg::DiagnosticStatus::STALE);
}

TEST_F(ReceiverNodeTest, RefreshesRuntimeFreshnessOnRuntimeObservationsWithoutStateMutation)
{
  const std::string best_nav = BuildUnicoreAsciiFrame(
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,"
      "1.1053,2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
      "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123");
  const std::string unknown_rtk_status = BuildUnicoreAsciiFrame(
      "#RTKSTATUSA,97,GPS,FINE,2190,365354000,0,0,18,1;"
      "0,0,0,0,0,0,0,0,0,0,0,UNKNOWN,5,0,99,12,0");

  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(best_nav),
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(unknown_rtk_status),
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(unknown_rtk_status),
       true},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "unicore"),
                                     rclcpp::Parameter("expected_runtime_observation_rate_hz", 1.0)});

  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();
  ASSERT_TRUE(node.last_fix_message().has_value());

  EXPECT_TRUE(node.StepOnce());

  std::this_thread::sleep_for(std::chrono::milliseconds(3100));

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* summary = FindDiagnosticStatusByName(diagnostics, "universal_gnss/summary");
  const auto* stale = FindDiagnosticStatusByName(diagnostics, "universal_gnss/runtime_state_stale");
  const auto* parser = FindDiagnosticStatusByName(diagnostics, "universal_gnss/parser_counters");

  ASSERT_NE(summary, nullptr);
  ASSERT_NE(parser, nullptr);
  EXPECT_EQ(stale, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*summary, "stale_data"), std::optional<std::string>{"false"});
  EXPECT_EQ(FindDiagnosticValue(*parser, "runtime_observations"), std::optional<std::string>{"3"});
  EXPECT_EQ(FindDiagnosticValue(*parser, "runtime_updates"), std::optional<std::string>{"2"});
  EXPECT_TRUE(node.last_fix_message().has_value());
}

TEST_F(ReceiverNodeTest, KeepsRuntimeStaleWhenOnlySemanticTrafficContinues)
{
  const std::string best_nav = BuildUnicoreAsciiFrame(
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,"
      "1.1053,2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
      "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123");
  const std::string rtcm_status = BuildUnicoreAsciiFrame(
      "#RTCMSTATUSA,76,GPS,FINE,2219,392572000,0,0,18,187;"
      "1124,21186,0,21,0,6,11,0,0,21");

  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(best_nav),
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(rtcm_status),
       true},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "unicore"),
                                     rclcpp::Parameter("expected_runtime_observation_rate_hz", 1.0)});

  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();
  ASSERT_TRUE(node.last_fix_message().has_value());

  std::this_thread::sleep_for(std::chrono::milliseconds(3100));

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* summary = FindDiagnosticStatusByName(diagnostics, "universal_gnss/summary");
  const auto* stale = FindDiagnosticStatusByName(diagnostics, "universal_gnss/runtime_state_stale");
  const auto* parser = FindDiagnosticStatusByName(diagnostics, "universal_gnss/parser_counters");

  ASSERT_NE(summary, nullptr);
  ASSERT_NE(stale, nullptr);
  ASSERT_NE(parser, nullptr);
  EXPECT_EQ(stale->level, diagnostic_msgs::msg::DiagnosticStatus::STALE);
  EXPECT_EQ(FindDiagnosticValue(*summary, "stale_data"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*parser, "runtime_observations"), std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*parser, "runtime_updates"), std::optional<std::string>{"1"});
  EXPECT_FALSE(node.last_fix_message().has_value());
}

TEST_F(ReceiverNodeTest, ForwardedRtcmSemanticTrafficDoesNotRefreshRuntimeFreshness)
{
  const std::string best_nav = BuildUnicoreAsciiFrame(
      "#BESTNAVA,97,GPS,FINE,2294,472312000,0,0,18,16;"
      "SOL_COMPUTED,NARROW_FLOAT,40.0789588272,116.2365102982,65.8312,-8.4925,WGS84,1.2221,"
      "1.1053,2.1970,\"0\",0.400,0.200,50,28,28,0,1,12,12,41,SOL_COMPUTED,DOPPLER_VELOCITY,"
      "0.000,0.000,0.0046,335.592288,0.0045,0.0194,0.0123");

  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildBytes(best_nav),
       true},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "unicore"),
                                     rclcpp::Parameter("expected_runtime_observation_rate_hz", 1.0)});

  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  auto publisher_node = std::make_shared<rclcpp::Node>("receiver_runtime_freshness_rtcm_publisher");
  auto publisher =
      publisher_node->create_publisher<universal_gnss_ros2::msg::RtcmFrame>("rtcm", 10);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node.get_node_base_interface());
  executor.add_node(publisher_node->get_node_base_interface());
  executor.spin_some();

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();
  ASSERT_TRUE(node.last_fix_message().has_value());

  std::this_thread::sleep_for(std::chrono::milliseconds(3100));

  universal_gnss_ros2::msg::RtcmFrame message;
  message.stamp.sec = 2000000000;
  message.stamp.nanosec = 456u;
  message.message_type = 1006u;
  message.data = BuildRtcm1006Frame(88u, 1000LL, -2000LL, 3000LL, 2500u);
  publisher->publish(message);

  for (std::size_t attempt = 0u; attempt < 8u; ++attempt)
  {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* summary = FindDiagnosticStatusByName(diagnostics, "universal_gnss/summary");
  const auto* stale = FindDiagnosticStatusByName(diagnostics, "universal_gnss/runtime_state_stale");
  const auto* parser = FindDiagnosticStatusByName(diagnostics, "universal_gnss/parser_counters");
  const auto* base_station =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/rtcm_semantic/base_station_arp");

  ASSERT_NE(summary, nullptr);
  ASSERT_NE(stale, nullptr);
  ASSERT_NE(parser, nullptr);
  ASSERT_NE(base_station, nullptr);
  EXPECT_EQ(stale->level, diagnostic_msgs::msg::DiagnosticStatus::STALE);
  EXPECT_EQ(FindDiagnosticValue(*summary, "stale_data"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*parser, "runtime_observations"), std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*parser, "runtime_updates"), std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*base_station, "decoded"), std::optional<std::string>{"true"});
  EXPECT_FALSE(node.last_fix_message().has_value());
}

TEST_F(ReceiverNodeTest, ReportsTransportReadErrorAndSuppressesStaleFix)
{
  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"),
       true},
      {universal_gnss_transport::TransportStatus::kError,
       universal_gnss_transport::TransportError::kReadFailure,
       {},
       true},
  });

  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "nmea")});

  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();
  ASSERT_TRUE(node.last_fix_message().has_value());

  EXPECT_FALSE(node.StepOnce());
  node.PublishNow();

  EXPECT_FALSE(node.last_fix_message().has_value());
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* summary = FindDiagnosticStatusByName(diagnostics, "universal_gnss/summary");
  const auto* read_error =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/transport_read_error");

  ASSERT_NE(summary, nullptr);
  ASSERT_NE(read_error, nullptr);
  EXPECT_EQ(read_error->level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(FindDiagnosticValue(*summary, "transport_healthy"),
            std::optional<std::string>{"false"});
}

TEST_F(ReceiverNodeTest, StaysAliveOnSerialOpenFailureAndReportsDiagnostic)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("transport", "serial"),
      rclcpp::Parameter("serial_device", "/definitely_missing_universal_gnss_device"),
      rclcpp::Parameter("serial_baud", 115200),
      rclcpp::Parameter("receiver_family", "ublox"),
  });

  universal_gnss_ros2::ReceiverNode node(options);
  EXPECT_FALSE(node.has_transport_source());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* summary = FindDiagnosticStatusByName(diagnostics, "universal_gnss/summary");
  const auto* open_failed =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/serial_open_failed");

  ASSERT_NE(summary, nullptr);
  ASSERT_NE(open_failed, nullptr);
  EXPECT_EQ(open_failed->level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(FindDiagnosticValue(*summary, "transport_healthy"),
            std::optional<std::string>{"false"});
}

TEST_F(ReceiverNodeTest, ReportsReceiverSideRtcmAcceptanceFromUbloxStream)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "ublox")});

  std::vector<std::uint8_t> stream =
      BuildUbxFrame(0x02u, 0x32u, MakeUbxRxmRtcmPayload(1077u, 42u, 0x04u));
  auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>(std::move(stream));
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* forwarding =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/rtcm_forwarding");
  const auto* receiver_rtcm =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/receiver_rtcm_active");

  ASSERT_NE(forwarding, nullptr);
  ASSERT_NE(receiver_rtcm, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_rtcm_messages_seen"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_rtcm_messages_used"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_correction_available"),
            std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_last_message_type"),
            std::optional<std::string>{"1077"});
  EXPECT_EQ(receiver_rtcm->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
}

TEST_F(ReceiverNodeTest, ReportsReceiverRtcmUseStaleAfterSilenceAndRecovers)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "ublox"),
                                     rclcpp::Parameter("rtcm_forwarding_activity_timeout_s", 0.05)});

  const auto accepted_rtcm =
      BuildUbxFrame(0x02u, 0x32u, MakeUbxRxmRtcmPayload(1077u, 42u, 0x04u));
  auto source = std::make_unique<ScriptedByteSource>(std::vector<ScriptedByteSource::Action>{
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       accepted_rtcm,
       true},
      {universal_gnss_transport::TransportStatus::kOk,
       universal_gnss_transport::TransportError::kNone,
       accepted_rtcm,
       true},
  });
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* active = FindDiagnosticStatusByName(*node.last_diagnostics_message(),
                                                   "universal_gnss/receiver_rtcm_active");
  ASSERT_NE(active, nullptr);
  EXPECT_EQ(active->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(active->message, "Receiver reported accepted RTCM corrections");

  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& silent_diagnostics = *node.last_diagnostics_message();
  const auto* stale = FindDiagnosticStatusByName(silent_diagnostics,
                                                 "universal_gnss/receiver_rtcm_stale");
  const auto* forwarding =
      FindDiagnosticStatusByName(silent_diagnostics, "universal_gnss/rtcm_forwarding");
  ASSERT_NE(stale, nullptr);
  ASSERT_NE(forwarding, nullptr);
  EXPECT_EQ(stale->level, diagnostic_msgs::msg::DiagnosticStatus::STALE);
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_correction_available"),
            std::optional<std::string>{"false"});

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& recovered_diagnostics = *node.last_diagnostics_message();
  const auto* recovered = FindDiagnosticStatusByName(recovered_diagnostics,
                                                      "universal_gnss/receiver_rtcm_active");
  const auto* recovered_forwarding =
      FindDiagnosticStatusByName(recovered_diagnostics, "universal_gnss/rtcm_forwarding");
  ASSERT_NE(recovered, nullptr);
  ASSERT_NE(recovered_forwarding, nullptr);
  EXPECT_EQ(recovered->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(recovered->message, "Receiver reported accepted RTCM corrections");
  EXPECT_EQ(FindDiagnosticValue(*recovered_forwarding, "receiver_correction_available"),
            std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*recovered_forwarding, "receiver_rtcm_messages_used"),
            std::optional<std::string>{"2"});
}

TEST_F(ReceiverNodeTest, ReportsReceiverSideRtcmStatusFromUnicoreStream)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "unicore")});

  const std::string rtcm_status =
      "#RTCMSTATUSA,76,GPS,FINE,2219,392572000,0,0,18,187;"
      "1124,21186,0,21,0,6,11,0,0,21*601a7581\r\n";
  auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>(
      std::vector<std::uint8_t>(rtcm_status.begin(), rtcm_status.end()));
  universal_gnss_ros2::ReceiverNode node(std::move(source), options);

  EXPECT_TRUE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* forwarding =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/rtcm_forwarding");
  const auto* receiver_rtcm =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/receiver_rtcm_active");

  ASSERT_NE(forwarding, nullptr);
  ASSERT_NE(receiver_rtcm, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_correction_available"),
            std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_rtcm_status_messages_seen"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_rtcm_status_message_count"),
            std::optional<std::string>{"21186"});
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_last_message_type"),
            std::optional<std::string>{"1124"});
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_last_base_station_id"),
            std::optional<std::string>{"0"});
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "receiver_last_satellites_in_message"),
            std::optional<std::string>{"21"});
  EXPECT_EQ(receiver_rtcm->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
}

TEST_F(ReceiverNodeTest, ConsumesRtcmTopicAndWritesCorrectionsToDuplexTransport)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "ublox"),
                                     rclcpp::Parameter("rtcm_forwarding_activity_timeout_s", 0.05)});

  auto duplex = std::make_unique<universal_gnss_transport::MemoryByteDuplex>();
  auto* duplex_ptr = duplex.get();
  universal_gnss_ros2::ReceiverNode node(std::move(duplex), options);

  auto publisher_node = std::make_shared<rclcpp::Node>("receiver_rtcm_publisher");
  auto publisher =
      publisher_node->create_publisher<universal_gnss_ros2::msg::RtcmFrame>("rtcm", 10);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node.get_node_base_interface());
  executor.add_node(publisher_node->get_node_base_interface());
  executor.spin_some();

  const auto bytes = BuildRtcmFrame(1077u);
  universal_gnss_ros2::msg::RtcmFrame message;
  message.message_type = 1077u;
  message.data = bytes;
  publisher->publish(message);
  executor.spin_some();

  EXPECT_EQ(duplex_ptr->written_bytes(), bytes);

  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* forwarding =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/rtcm_forwarding");
  ASSERT_NE(forwarding, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "forwarded_frame_count"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "last_message_type"),
            std::optional<std::string>{"1077"});

  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* stale = FindDiagnosticStatusByName(*node.last_diagnostics_message(),
                                                  "universal_gnss/rtcm_forwarding");
  ASSERT_NE(stale, nullptr);
  EXPECT_EQ(stale->level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(stale->message, "RTCM forwarding stale");
  EXPECT_EQ(FindDiagnosticValue(*stale, "forwarded_frame_count"),
            std::optional<std::string>{"1"});

  publisher->publish(message);
  for (std::size_t attempt = 0u;
       attempt < 8u && duplex_ptr->written_bytes().size() < bytes.size() * 2u;
       ++attempt)
  {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_EQ(duplex_ptr->written_bytes().size(), bytes.size() * 2u);

  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* recovered = FindDiagnosticStatusByName(*node.last_diagnostics_message(),
                                                      "universal_gnss/rtcm_forwarding");
  ASSERT_NE(recovered, nullptr);
  EXPECT_EQ(recovered->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(recovered->message, "RTCM forwarding active");
  EXPECT_EQ(FindDiagnosticValue(*recovered, "forwarded_frame_count"),
            std::optional<std::string>{"2"});
}

TEST_F(ReceiverNodeTest, ProjectsForwardedRtcmSemanticObservationsIntoDiagnostics)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "nmea")});

  auto duplex = std::make_unique<universal_gnss_transport::MemoryByteDuplex>();
  auto* duplex_ptr = duplex.get();
  universal_gnss_ros2::ReceiverNode node(std::move(duplex), options);

  auto publisher_node = std::make_shared<rclcpp::Node>("receiver_rtcm_semantic_publisher");
  auto publisher =
      publisher_node->create_publisher<universal_gnss_ros2::msg::RtcmFrame>("rtcm", 10);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node.get_node_base_interface());
  executor.add_node(publisher_node->get_node_base_interface());
  executor.spin_some();

  const auto rtcm_1006 = BuildRtcm1006Frame(88u, 1000LL, -2000LL, 3000LL, 2500u);
  const auto rtcm_1230 = BuildRtcm1230Frame(
      88u, true, true, false, true, false, 10, std::nullopt, -15, std::nullopt);
  auto malformed_1230_payload = std::vector<std::uint8_t>{
      static_cast<std::uint8_t>((1230u >> 4u) & 0xFFu),
      static_cast<std::uint8_t>((1230u & 0x0Fu) << 4u),
  };
  malformed_1230_payload.push_back(0x00u);
  const auto malformed_1230 = BuildRtcmFrameFromPayload(malformed_1230_payload);
  const auto rtcm_1077 = BuildRtcmMsmFrame(1077u, 88u, {1u}, {2u}, {true});

  universal_gnss_ros2::msg::RtcmFrame message;
  message.stamp.sec = 123;
  message.stamp.nanosec = 456u;

  message.message_type = 1006u;
  message.data = rtcm_1006;
  publisher->publish(message);

  message.message_type = 1230u;
  message.data = rtcm_1230;
  publisher->publish(message);

  message.message_type = 1230u;
  message.data = malformed_1230;
  publisher->publish(message);

  message.message_type = 1077u;
  message.data = rtcm_1077;
  publisher->publish(message);

  for (std::size_t attempt = 0u; attempt < 8u; ++attempt)
  {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  EXPECT_FALSE(duplex_ptr->written_bytes().empty());

  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();

  const auto* base_station =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/rtcm_semantic/base_station_arp");
  ASSERT_NE(base_station, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*base_station, "decoded"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*base_station, "station_id"), std::optional<std::string>{"88"});

  const auto* glonass_bias = FindDiagnosticStatusByName(
      diagnostics, "universal_gnss/rtcm_semantic/glonass_code_phase_bias");
  ASSERT_NE(glonass_bias, nullptr);
  EXPECT_EQ(glonass_bias->level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(FindDiagnosticValue(*glonass_bias, "decoded"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*glonass_bias, "valid"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*glonass_bias, "decode_success_count"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*glonass_bias, "decode_failure_count"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*glonass_bias, "malformed_count"),
            std::optional<std::string>{"1"});

  const auto* msm_summary =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss/rtcm_semantic/msm_summary");
  ASSERT_NE(msm_summary, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "message_type"),
            std::optional<std::string>{"1077"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "station_id"), std::optional<std::string>{"88"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "constellations_seen"),
            std::optional<std::string>{"gps"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "satellite_count"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "signal_count"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "cell_count"), std::optional<std::string>{"1"});
  const auto msm_age_ns = FindDiagnosticValue(*msm_summary, "age_ns");
  ASSERT_NE(msm_age_ns, std::nullopt);
  EXPECT_GE(std::stoll(*msm_age_ns), 0)
      << "RTCM freshness must never subtract a public ROS stamp from steady now";
  EXPECT_LT(std::stoll(*msm_age_ns), 1000000000LL)
      << "freshly received RTCM should have a small local monotonic age";

  message.stamp.sec = 1;
  message.stamp.nanosec = 0u;
  publisher->publish(message);
  for (std::size_t attempt = 0u; attempt < 4u; ++attempt)
  {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* recovered_msm = FindDiagnosticStatusByName(
      *node.last_diagnostics_message(), "universal_gnss/rtcm_semantic/msm_summary");
  ASSERT_NE(recovered_msm, nullptr);
  const auto recovered_age_ns = FindDiagnosticValue(*recovered_msm, "age_ns");
  ASSERT_NE(recovered_age_ns, std::nullopt);
  EXPECT_GE(std::stoll(*recovered_age_ns), 0);
  EXPECT_LT(std::stoll(*recovered_age_ns), 1000000000LL)
      << "a ROS-stamp jump must not prevent monotonic RTCM freshness recovery";
}

}  // namespace
