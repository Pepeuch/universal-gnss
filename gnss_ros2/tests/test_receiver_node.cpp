#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "rclcpp/rclcpp.hpp"
#include "universal_gnss_protocols/nmea_checksum.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"
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
  EXPECT_TRUE(std::isnan(status.latitude_deg));
  EXPECT_TRUE(std::isnan(status.heading_deg));
  EXPECT_EQ(status.capability_flags &
                universal_gnss_ros2::msg::GnssStatus::CAP_HEADING,
            0u);
}

}  // namespace
