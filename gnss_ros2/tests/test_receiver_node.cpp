#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "rclcpp/rclcpp.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"
#if defined(__linux__) && defined(UNIVERSAL_GNSS_TRANSPORT_HAS_TCP_CLIENT)
#include "universal_gnss_ros2/ntrip_node.hpp"
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

}  // namespace
