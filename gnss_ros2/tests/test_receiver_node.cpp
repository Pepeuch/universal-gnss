#include <chrono>
#include <cmath>
#include <cstdint>
#include <cerrno>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "rclcpp/rclcpp.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_protocols/rtcm_crc24q.hpp"
#include "universal_gnss_protocols/ubx_checksum.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"
#include "universal_gnss_ros2/msg/rtcm_frame.hpp"
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

void WriteLeU2(std::vector<std::uint8_t>& payload, const std::size_t offset, const std::uint16_t value)
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
    universal_gnss_transport::TransportError error{
        universal_gnss_transport::TransportError::kNone};
    std::vector<std::uint8_t> payload{};
    bool keep_open{true};
  };

  explicit ScriptedByteSource(std::vector<Action> actions) : actions_(std::move(actions)) {}

  universal_gnss_transport::ReadResult Read(std::uint8_t* destination, std::size_t capacity) override
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

  bool IsOpen() const override { return open_; }

  void Close() override { open_ = false; }

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
  result.evidence.bytes_read = 128u;
  if (family == universal_gnss_driver::ReceiverDetectedFamily::kUblox)
  {
    result.evidence.ubx_frames_seen = 1u;
  }
  else if (family == universal_gnss_driver::ReceiverDetectedFamily::kUnicore)
  {
    result.evidence.unicore_binary_seen = 1u;
  }
  else if (family == universal_gnss_driver::ReceiverDetectedFamily::kNmea)
  {
    result.evidence.nmea_sentences_seen = 1u;
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
                       const universal_gnss_driver::ReceiverDiscoveryPaths&) {
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
                       const universal_gnss_driver::ReceiverDiscoveryPaths&) {
    captured_path = explicit_path;
    captured_include_platform = config.include_platform_uarts;
    return std::vector<universal_gnss_driver::ReceiverProbeResult>{
        MakeDiscoveryResult("/dev/serial/by-id/f9p", 921600u,
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
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "baud"),
            std::optional<std::string>{"921600"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "family"),
            std::optional<std::string>{"ublox"});
  EXPECT_EQ(FindDiagnosticValue(*discovery_status, "confidence"),
            std::optional<std::string>{"high"});
}

TEST_F(ReceiverNodeTest, ExplicitPathWithAutoBaudAndFamilyProbesOnlyThatPath)
{
  std::optional<std::string> captured_path;
  std::vector<std::uint32_t> captured_bauds;
  auto discovery = [&](const universal_gnss_driver::ReceiverProbeConfig& config,
                       const std::optional<std::string>& explicit_path,
                       const universal_gnss_driver::ReceiverDiscoveryPaths&) {
    captured_path = explicit_path;
    captured_bauds = config.baud_candidates;
    return std::vector<universal_gnss_driver::ReceiverProbeResult>{
        MakeDiscoveryResult("/dev/ttyAMA2", 921600u,
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
                       const universal_gnss_driver::ReceiverDiscoveryPaths&) {
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
                       const universal_gnss_driver::ReceiverDiscoveryPaths&) {
    return std::vector<universal_gnss_driver::ReceiverProbeResult>{
        MakeDiscoveryResult("/dev/ttyS1", 921600u,
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
  auto make_discovery = []() {
    return [](const universal_gnss_driver::ReceiverProbeConfig&,
              const std::optional<std::string>&,
              const universal_gnss_driver::ReceiverDiscoveryPaths&) {
      return std::vector<universal_gnss_driver::ReceiverProbeResult>{
          MakeDiscoveryResult("/dev/ttyAMA2", 921600u,
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
    EXPECT_EQ(FindDiagnosticValue(*discovery_status, "family"),
              std::optional<std::string>{"nmea"});
  }
}

TEST_F(ReceiverNodeTest, DiscoveryReceivesPlatformUartOptInAndKnownBaud)
{
  bool include_platform_uarts = false;
  std::vector<std::uint32_t> captured_bauds;
  auto discovery = [&](const universal_gnss_driver::ReceiverProbeConfig& config,
                       const std::optional<std::string>& explicit_path,
                       const universal_gnss_driver::ReceiverDiscoveryPaths&) {
    include_platform_uarts = config.include_platform_uarts;
    captured_bauds = config.baud_candidates;
    EXPECT_EQ(explicit_path, std::optional<std::string>{"/dev/ttyAMA2"});
    return std::vector<universal_gnss_driver::ReceiverProbeResult>{
        MakeDiscoveryResult("/dev/ttyAMA2", 921600u,
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
  const auto* forwarding =
      FindDiagnosticStatusByName(*receiver.last_diagnostics_message(), "universal_gnss/rtcm_forwarding");
  ASSERT_NE(forwarding, nullptr);
  EXPECT_EQ(FindDiagnosticValue(*forwarding, "forwarded_frame_count"),
            std::optional<std::string>{"1"});
}
#endif

TEST_F(ReceiverNodeTest, ProjectsRuntimeUpdatesThroughRosAdapters)
{
  std::vector<std::uint8_t> stream;
  AppendBytes(
      stream,
      BuildNmeaSentence("GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
  AppendBytes(stream, BuildNmeaSentence("GPGSA,A,3,04,05,09,12,24,25,29,31,,,,,1.8,1.0,1.5"));
  AppendBytes(stream,
              BuildNmeaSentence("GPGSV,2,1,08,01,40,083,41,02,17,308,43,12,25,120,42,14,10,220,39"));
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
  EXPECT_TRUE(
      diagnostics.header.stamp.sec != 0 || diagnostics.header.stamp.nanosec != 0u);
  EXPECT_EQ(diagnostics.header.frame_id, "gnss");
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
  EXPECT_EQ(status.capability_flags &
                universal_gnss_ros2::msg::GnssStatus::CAP_HEADING,
            0u);
}

TEST_F(ReceiverNodeTest, RejectsInvalidReceiverFamily)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "mystery")});

  auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>();
  EXPECT_THROW(universal_gnss_ros2::ReceiverNode(std::move(source), options), std::invalid_argument);
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
    options.parameter_overrides(
        std::vector<rclcpp::Parameter>{rclcpp::Parameter("frame_id", "")});
    auto source = std::make_unique<universal_gnss_transport::MemoryByteSource>();
    EXPECT_THROW(universal_gnss_ros2::ReceiverNode(std::move(source), options),
                 std::invalid_argument);
  }
}

TEST_F(ReceiverNodeTest, ReportsNoDataReceivedAfterGracePeriod)
{
  auto source = std::make_unique<ScriptedByteSource>(
      std::vector<ScriptedByteSource::Action>{{}});
  universal_gnss_ros2::ReceiverNode node(std::move(source));

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
  EXPECT_EQ(FindDiagnosticValue(*summary, "transport_healthy"), std::optional<std::string>{"false"});
  EXPECT_EQ(FindDiagnosticValue(*summary, "stale_data"), std::optional<std::string>{"false"});
}

TEST_F(ReceiverNodeTest, ReportsTransportReadErrorAndSuppressesStaleFix)
{
  auto source = std::make_unique<ScriptedByteSource>(
      std::vector<ScriptedByteSource::Action>{
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
  EXPECT_EQ(FindDiagnosticValue(*summary, "transport_healthy"), std::optional<std::string>{"false"});
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
  EXPECT_EQ(FindDiagnosticValue(*summary, "transport_healthy"), std::optional<std::string>{"false"});
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
      std::vector<rclcpp::Parameter>{rclcpp::Parameter("receiver_family", "ublox")});

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
}

}  // namespace
