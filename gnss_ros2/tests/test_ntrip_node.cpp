#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "rclcpp/rclcpp.hpp"
#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_ntrip/ntrip_request.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/rtcm_parser.hpp"
#include "universal_gnss_ros2/gnss_status_adapter.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"
#include "universal_gnss_ros2/msg/rtcm_frame.hpp"
#include "universal_gnss_ros2/ntrip_node.hpp"

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{

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

universal_gnss_ros2::msg::GnssStatus MakeGnssStatus()
{
  universal_gnss::GnssRuntimeState state;
  state.fix_valid = true;
  state.fix_type = universal_gnss::GnssFixType::kFix;
  state.latitude_deg = 48.1173;
  state.longitude_deg = 11.5166667;
  state.altitude_m = 545.4;
  state.hdop = 0.9f;
  state.satellites_used = 8u;
  return universal_gnss_ros2::ToGnssStatusMessage(state);
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

  std::uint32_t crc =
      universal_gnss_protocols::ComputeRtcmCrc24Q(bytes.data(), bytes.size());
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

std::vector<std::uint8_t> BuildRtcmMsmFrame(const std::uint16_t message_type,
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
  return BuildRtcmFrameFromPayload(payload);
}

class NtripNodeTest : public ::testing::Test
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

TEST_F(NtripNodeTest, ConstructsWithParametersAndDiagnosticsPublisherReady)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("caster_host", "caster.example.com"),
      rclcpp::Parameter("caster_port", 2101),
      rclcpp::Parameter("mountpoint", "RTCM3"),
      rclcpp::Parameter("gga_enabled", true),
      rclcpp::Parameter("gga_interval_s", 5),
  });

  universal_gnss_ros2::NtripNode node(options);

  EXPECT_TRUE(node.diagnostics_ready());
  EXPECT_EQ(node.get_parameter("caster_host").as_string(), "caster.example.com");
  EXPECT_EQ(node.get_parameter("caster_port").as_int(), 2101);
  EXPECT_EQ(node.get_parameter("mountpoint").as_string(), "RTCM3");
  EXPECT_TRUE(node.get_parameter("gga_enabled").as_bool());
  EXPECT_EQ(node.get_parameter("gga_interval_s").as_int(), 5);
}

TEST_F(NtripNodeTest, RejectsInvalidParameters)
{
  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(std::vector<rclcpp::Parameter>{
        rclcpp::Parameter("caster_host", ""),
        rclcpp::Parameter("mountpoint", "RTCM3"),
    });
    EXPECT_THROW(universal_gnss_ros2::NtripNode(options), std::invalid_argument);
  }

  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(std::vector<rclcpp::Parameter>{
        rclcpp::Parameter("caster_host", "caster.example.com"),
        rclcpp::Parameter("caster_port", 0),
        rclcpp::Parameter("mountpoint", "RTCM3"),
    });
    EXPECT_THROW(universal_gnss_ros2::NtripNode(options), std::invalid_argument);
  }

  {
    rclcpp::NodeOptions options;
    options.parameter_overrides(std::vector<rclcpp::Parameter>{
        rclcpp::Parameter("caster_host", "caster.example.com"),
        rclcpp::Parameter("mountpoint", "RTCM3"),
        rclcpp::Parameter("tls_enabled", true),
    });
    EXPECT_THROW(universal_gnss_ros2::NtripNode(options), std::invalid_argument);
  }
}

#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)

class SocketPair
{
public:
  ~SocketPair()
  {
    ClosePeer();
    CloseClient();
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
      const ssize_t bytes_written =
          ::write(peer_fd_, data.data() + static_cast<std::ptrdiff_t>(offset), data.size() - offset);
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

  std::string ReadPeerText(const std::size_t max_bytes,
                           const std::chrono::milliseconds timeout = std::chrono::milliseconds(500))
  {
    std::vector<char> buffer(max_bytes, '\0');
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline)
    {
      const ssize_t bytes_read =
          ::recv(peer_fd_, buffer.data(), buffer.size(), MSG_DONTWAIT);
      if (bytes_read > 0)
      {
        return std::string(buffer.data(), buffer.data() + bytes_read);
      }
      if (bytes_read < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
      {
        return {};
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return {};
  }

  void ClosePeer()
  {
    if (peer_fd_ >= 0)
    {
      ::close(peer_fd_);
      peer_fd_ = -1;
    }
  }

  void CloseClient()
  {
    if (client_fd_ >= 0)
    {
      ::close(client_fd_);
      client_fd_ = -1;
    }
  }

private:
  int client_fd_{-1};
  int peer_fd_{-1};
};

void DeliverStatus(universal_gnss_ros2::NtripNode& node,
                   const std::shared_ptr<rclcpp::Node>& publisher_node,
                   rclcpp::executors::SingleThreadedExecutor& executor,
                   const universal_gnss_ros2::msg::GnssStatus& status)
{
  auto publisher =
      publisher_node->create_publisher<universal_gnss_ros2::msg::GnssStatus>("status", 10);
  executor.add_node(publisher_node->get_node_base_interface());
  executor.add_node(node.get_node_base_interface());
  executor.spin_some();
  publisher->publish(status);
  executor.spin_some();
  executor.remove_node(node.get_node_base_interface());
  executor.remove_node(publisher_node->get_node_base_interface());
}

TEST_F(NtripNodeTest, ForwardsGnssStatusToRealGgaInjectionAndDiagnostics)
{
  SocketPair sockets;
  ASSERT_TRUE(sockets.Open());

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("caster_host", "caster.example.com"),
      rclcpp::Parameter("caster_port", 2101),
      rclcpp::Parameter("mountpoint", "RTCM3"),
      rclcpp::Parameter("gga_enabled", true),
      rclcpp::Parameter("gga_interval_s", 1),
  });

  universal_gnss_ros2::NtripNode node(sockets.ReleaseClientFd(), options);
  ASSERT_TRUE(node.client_ready());

  auto publisher_node = std::make_shared<rclcpp::Node>("ntrip_status_publisher");
  rclcpp::executors::SingleThreadedExecutor executor;
  DeliverStatus(node, publisher_node, executor, MakeGnssStatus());
  EXPECT_TRUE(node.has_runtime_state());

  EXPECT_TRUE(node.StepOnce());
  const std::string request_text = sockets.ReadPeerText(1024u);
  EXPECT_NE(request_text.find("GET /RTCM3 HTTP/1.1"), std::string::npos);
  EXPECT_NE(request_text.find("Ntrip-Version: Ntrip/2.0"), std::string::npos);

  ASSERT_TRUE(sockets.WritePeer("ICY 200 OK\r\nNtrip-Version: Ntrip/2.0\r\n\r\n"));
  EXPECT_TRUE(node.StepOnce());
  const std::string gga_text = sockets.ReadPeerText(1024u);
  EXPECT_NE(gga_text.find("$GPGGA"), std::string::npos);

  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  EXPECT_NE(FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/ntrip_streaming"),
            nullptr);
  EXPECT_NE(
      FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/gga_injection_active"),
      nullptr);
}

TEST_F(NtripNodeTest, PublishesRtcmFramesForReceiverForwarding)
{
  SocketPair sockets;
  ASSERT_TRUE(sockets.Open());

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("caster_host", "caster.example.com"),
      rclcpp::Parameter("caster_port", 2101),
      rclcpp::Parameter("mountpoint", "RTCM3"),
      rclcpp::Parameter("gga_enabled", false),
  });

  universal_gnss_ros2::NtripNode node(sockets.ReleaseClientFd(), options);
  ASSERT_TRUE(node.client_ready());

  EXPECT_TRUE(node.StepOnce());
  const auto request_text = sockets.ReadPeerText(1024u);
  EXPECT_NE(request_text.find("GET /RTCM3 HTTP/1.1"), std::string::npos);

  const auto rtcm = BuildRtcmFrame(1077u);
  ASSERT_TRUE(sockets.WritePeer("ICY 200 OK\r\n"));
  ASSERT_TRUE(sockets.WritePeer(rtcm));
  for (std::size_t attempt = 0u; attempt < 8u && !node.last_rtcm_message().has_value(); ++attempt)
  {
    node.StepOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  ASSERT_TRUE(node.last_rtcm_message().has_value());
  EXPECT_EQ(node.last_rtcm_message()->message_type, 1077u);
  EXPECT_EQ(node.last_rtcm_message()->data, rtcm);

  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* forwarding =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/rtcm_forwarding");
  ASSERT_NE(forwarding, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "published_frame_count"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "last_message_type"),
            std::optional<std::string>{"1077"});
  EXPECT_NE(FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/rtcm_forwarding_active"),
            nullptr);
}

TEST_F(NtripNodeTest, ReportsRtcmForwardingStaleAfterSilenceAndRecovers)
{
  SocketPair sockets;
  ASSERT_TRUE(sockets.Open());

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("caster_host", "caster.example.com"),
      rclcpp::Parameter("caster_port", 2101),
      rclcpp::Parameter("mountpoint", "RTCM3"),
      rclcpp::Parameter("gga_enabled", false),
      rclcpp::Parameter("rtcm_forwarding_activity_timeout_s", 0.05),
  });

  universal_gnss_ros2::NtripNode node(sockets.ReleaseClientFd(), options);
  ASSERT_TRUE(node.client_ready());

  EXPECT_TRUE(node.StepOnce());
  EXPECT_NE(sockets.ReadPeerText(1024u).find("GET /RTCM3 HTTP/1.1"), std::string::npos);

  const auto first_frame = BuildRtcmFrame(1077u);
  ASSERT_TRUE(sockets.WritePeer("ICY 200 OK\r\n"));
  ASSERT_TRUE(sockets.WritePeer(first_frame));
  for (std::size_t attempt = 0u; attempt < 8u && !node.last_rtcm_message().has_value(); ++attempt)
  {
    node.StepOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(node.last_rtcm_message().has_value());

  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* active = FindDiagnosticStatusByName(
      *node.last_diagnostics_message(), "universal_gnss_ntrip/rtcm_forwarding");
  ASSERT_NE(active, nullptr);
  EXPECT_EQ(active->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(active->message, "RTCM forwarding active");

  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& silent_diagnostics = *node.last_diagnostics_message();
  const auto* stale =
      FindDiagnosticStatusByName(silent_diagnostics, "universal_gnss_ntrip/rtcm_forwarding");
  ASSERT_NE(stale, nullptr);
  EXPECT_EQ(stale->level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(stale->message, "RTCM forwarding stale");
  EXPECT_EQ(FindDiagnosticValue(*stale, "published_frame_count"),
            std::optional<std::string>{"1"});
  EXPECT_NE(FindDiagnosticStatusByName(silent_diagnostics, "universal_gnss_ntrip/ntrip_streaming"),
            nullptr);

  const auto second_frame = BuildRtcmFrame(1087u);
  ASSERT_TRUE(sockets.WritePeer(second_frame));
  for (std::size_t attempt = 0u; attempt < 8u; ++attempt)
  {
    node.StepOnce();
    if (node.last_rtcm_message().has_value() && node.last_rtcm_message()->message_type == 1087u)
    {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  ASSERT_TRUE(node.last_rtcm_message().has_value());
  EXPECT_EQ(node.last_rtcm_message()->message_type, 1087u);

  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto* recovered = FindDiagnosticStatusByName(
      *node.last_diagnostics_message(), "universal_gnss_ntrip/rtcm_forwarding");
  ASSERT_NE(recovered, nullptr);
  EXPECT_EQ(recovered->level, diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(recovered->message, "RTCM forwarding active");
  EXPECT_EQ(FindDiagnosticValue(*recovered, "published_frame_count"),
            std::optional<std::string>{"2"});
}

TEST_F(NtripNodeTest, ProjectsRtcmSemanticObservationsIntoDiagnostics)
{
  SocketPair sockets;
  ASSERT_TRUE(sockets.Open());

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("caster_host", "caster.example.com"),
      rclcpp::Parameter("caster_port", 2101),
      rclcpp::Parameter("mountpoint", "RTCM3"),
      rclcpp::Parameter("gga_enabled", false),
  });

  universal_gnss_ros2::NtripNode node(sockets.ReleaseClientFd(), options);
  ASSERT_TRUE(node.client_ready());

  EXPECT_TRUE(node.StepOnce());
  ASSERT_TRUE(sockets.WritePeer("ICY 200 OK\r\nNtrip-Version: Ntrip/2.0\r\n\r\n"));

  const auto rtcm_1006 = BuildRtcm1006Frame(42u, 1234567LL, -2345678LL, 3456789LL, 4321u);
  const auto rtcm_1230 = BuildRtcm1230Frame(
      42u, true, true, false, true, false, 25, std::nullopt, -10, std::nullopt);
  const auto rtcm_1077 = BuildRtcmMsmFrame(1077u, 42u, {1u, 3u}, {2u}, {true, false});
  const auto rtcm_1087 = BuildRtcmMsmFrame(1087u, 42u, {4u}, {1u, 2u}, {true, true});

  ASSERT_TRUE(sockets.WritePeer(rtcm_1006));
  ASSERT_TRUE(sockets.WritePeer(rtcm_1230));
  ASSERT_TRUE(sockets.WritePeer(rtcm_1077));
  ASSERT_TRUE(sockets.WritePeer(rtcm_1087));

  for (std::size_t attempt = 0u; attempt < 8u; ++attempt)
  {
    node.StepOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();

  const auto* base_station =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/rtcm_semantic/base_station_arp");
  ASSERT_NE(base_station, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*base_station, "seen"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*base_station, "decoded"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*base_station, "message_type"),
            std::optional<std::string>{"1006"});
  EXPECT_EQ(FindDiagnosticValue(*base_station, "station_id"), std::optional<std::string>{"42"});

  const auto* glonass_bias = FindDiagnosticStatusByName(
      diagnostics, "universal_gnss_ntrip/rtcm_semantic/glonass_code_phase_bias");
  ASSERT_NE(glonass_bias, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*glonass_bias, "decoded"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*glonass_bias, "valid"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*glonass_bias, "signal_mask"), std::optional<std::string>{"0x5"});

  const auto* msm_summary =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/rtcm_semantic/msm_summary");
  ASSERT_NE(msm_summary, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "seen"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "decoded"), std::optional<std::string>{"true"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "message_type"),
            std::optional<std::string>{"1087"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "station_id"), std::optional<std::string>{"42"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "constellations_seen"),
            std::optional<std::string>{"gps,glonass"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "satellite_count"),
            std::optional<std::string>{"1"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "signal_count"),
            std::optional<std::string>{"2"});
  EXPECT_EQ(FindDiagnosticValue(*msm_summary, "cell_count"), std::optional<std::string>{"2"});
  EXPECT_NE(FindDiagnosticValue(*msm_summary, "age_ns"), std::nullopt);

  EXPECT_NE(FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/rtcm_semantic/msm_gps_msm7"),
            nullptr);
  EXPECT_NE(FindDiagnosticStatusByName(
                diagnostics, "universal_gnss_ntrip/rtcm_semantic/msm_glonass_msm7"),
            nullptr);
}

TEST_F(NtripNodeTest, ReportsReconnectStateAfterStreamDisconnect)
{
  SocketPair sockets;
  ASSERT_TRUE(sockets.Open());

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("caster_host", "127.0.0.1"),
      rclcpp::Parameter("caster_port", 2101),
      rclcpp::Parameter("mountpoint", "RTCM3"),
      rclcpp::Parameter("gga_enabled", false),
  });

  universal_gnss_ros2::NtripNode node(sockets.ReleaseClientFd(), options);
  EXPECT_TRUE(node.StepOnce());
  ASSERT_TRUE(sockets.WritePeer("ICY 200 OK\r\nNtrip-Version: Ntrip/2.0\r\n\r\n"));
  node.StepOnce();

  sockets.ClosePeer();
  EXPECT_FALSE(node.StepOnce());
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  const auto* reconnecting =
      FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/ntrip_reconnecting");
  ASSERT_NE(reconnecting, nullptr);
  EXPECT_EQ(reconnecting->level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  const auto* summary = FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/summary");
  ASSERT_NE(summary, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*summary, "transport_healthy"),
            std::optional<std::string>{"false"});
}

TEST_F(NtripNodeTest, DoesNotInjectGgaWithoutStatusAndReportsMissingSource)
{
  SocketPair sockets;
  ASSERT_TRUE(sockets.Open());

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("caster_host", "caster.example.com"),
      rclcpp::Parameter("caster_port", 2101),
      rclcpp::Parameter("mountpoint", "RTCM3"),
      rclcpp::Parameter("gga_enabled", true),
      rclcpp::Parameter("gga_interval_s", 1),
  });

  universal_gnss_ros2::NtripNode node(sockets.ReleaseClientFd(), options);
  ASSERT_TRUE(node.client_ready());

  EXPECT_TRUE(node.StepOnce());
  ASSERT_TRUE(sockets.WritePeer("ICY 200 OK\r\nNtrip-Version: Ntrip/2.0\r\n\r\n"));
  node.StepOnce();

  const std::string peer_text = sockets.ReadPeerText(1024u);
  EXPECT_EQ(peer_text.find("$GPGGA"), std::string::npos);

  std::this_thread::sleep_for(std::chrono::milliseconds(3200));
  node.PublishNow();

  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  EXPECT_NE(FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/gga_source_missing"),
            nullptr);
}

TEST_F(NtripNodeTest, DoesNotInjectGgaWhenStatusIsStaleAndReportsStaleSource)
{
  SocketPair sockets;
  ASSERT_TRUE(sockets.Open());

  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      rclcpp::Parameter("caster_host", "caster.example.com"),
      rclcpp::Parameter("caster_port", 2101),
      rclcpp::Parameter("mountpoint", "RTCM3"),
      rclcpp::Parameter("gga_enabled", true),
      rclcpp::Parameter("gga_interval_s", 1),
  });

  universal_gnss_ros2::NtripNode node(sockets.ReleaseClientFd(), options);
  ASSERT_TRUE(node.client_ready());

  auto publisher_node = std::make_shared<rclcpp::Node>("ntrip_stale_status_publisher");
  rclcpp::executors::SingleThreadedExecutor executor;
  DeliverStatus(node, publisher_node, executor, MakeGnssStatus());
  EXPECT_TRUE(node.has_runtime_state());

  EXPECT_TRUE(node.StepOnce());
  std::this_thread::sleep_for(std::chrono::milliseconds(5200));
  ASSERT_TRUE(sockets.WritePeer("ICY 200 OK\r\nNtrip-Version: Ntrip/2.0\r\n\r\n"));
  node.StepOnce();

  const std::string peer_text = sockets.ReadPeerText(1024u);
  EXPECT_EQ(peer_text.find("$GPGGA"), std::string::npos);

  node.PublishNow();
  ASSERT_TRUE(node.last_diagnostics_message().has_value());
  const auto& diagnostics = *node.last_diagnostics_message();
  EXPECT_NE(FindDiagnosticStatusByName(diagnostics, "universal_gnss_ntrip/gga_source_stale"),
            nullptr);
}

#endif

}  // namespace
