#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "universal_gnss/gnss_diagnostic.hpp"
#include "universal_gnss/gnss_health.hpp"
#include "universal_gnss_ros2/diagnostic_adapter.hpp"

namespace
{

const diagnostic_msgs::msg::KeyValue* FindKeyValue(
    const diagnostic_msgs::msg::DiagnosticStatus& status,
    const std::string& key)
{
  for (const auto& entry : status.values)
  {
    if (entry.key == key)
    {
      return &entry;
    }
  }

  return nullptr;
}

TEST(DiagnosticAdapterTest, MapsPortableSeveritiesToRosDiagnosticLevels)
{
  using universal_gnss::GnssDiagnosticSeverity;

  EXPECT_EQ(universal_gnss_ros2::ToDiagnosticLevel(GnssDiagnosticSeverity::kOk),
            diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(universal_gnss_ros2::ToDiagnosticLevel(GnssDiagnosticSeverity::kInfo),
            diagnostic_msgs::msg::DiagnosticStatus::OK);
  EXPECT_EQ(universal_gnss_ros2::ToDiagnosticLevel(GnssDiagnosticSeverity::kWarning),
            diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(universal_gnss_ros2::ToDiagnosticLevel(GnssDiagnosticSeverity::kError),
            diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  EXPECT_EQ(universal_gnss_ros2::ToDiagnosticLevel(GnssDiagnosticSeverity::kStale),
            diagnostic_msgs::msg::DiagnosticStatus::STALE);
}

TEST(DiagnosticAdapterTest, MapsSingleDiagnosticEventWithPortableMetadata)
{
  universal_gnss::GnssDiagnosticEvent event;
  event.severity = universal_gnss::GnssDiagnosticSeverity::kWarning;
  event.category = universal_gnss::GnssDiagnosticCategory::kReceiver;
  event.code = "antenna_open";
  event.message = "Receiver antenna open";
  event.timestamp_ns = 4200000007LL;
  event.source = "ublox";

  const auto status =
      universal_gnss_ros2::ToDiagnosticStatusMessage(event, "gnss", "receiver-1");

  EXPECT_EQ(status.level, diagnostic_msgs::msg::DiagnosticStatus::WARN);
  EXPECT_EQ(status.name, "gnss/antenna_open");
  EXPECT_EQ(status.message, "Receiver antenna open");
  EXPECT_EQ(status.hardware_id, "receiver-1");

  const auto* severity = FindKeyValue(status, "original_severity");
  ASSERT_NE(severity, nullptr);
  EXPECT_EQ(severity->value, "warning");

  const auto* category = FindKeyValue(status, "category");
  ASSERT_NE(category, nullptr);
  EXPECT_EQ(category->value, "receiver");

  const auto* source = FindKeyValue(status, "source");
  ASSERT_NE(source, nullptr);
  EXPECT_EQ(source->value, "ublox");

  const auto* timestamp = FindKeyValue(status, "timestamp_ns");
  ASSERT_NE(timestamp, nullptr);
  EXPECT_EQ(timestamp->value, "4200000007");
}

TEST(DiagnosticAdapterTest, BuildsDiagnosticArrayWithSummaryAndLatestTimestamp)
{
  universal_gnss::GnssHealthSummary summary;
  summary.fix_valid = true;
  summary.rtk_available = true;
  summary.correction_available = true;
  summary.receiver_healthy = false;
  summary.transport_healthy = true;
  summary.parser_healthy = true;

  universal_gnss::GnssDiagnosticEvent warning_event;
  warning_event.severity = universal_gnss::GnssDiagnosticSeverity::kWarning;
  warning_event.category = universal_gnss::GnssDiagnosticCategory::kCorrection;
  warning_event.code = "rtcm_not_used";
  warning_event.message = "Receiver saw RTCM but did not use it";
  warning_event.timestamp_ns = 1000000000LL;
  summary.AddEvent(warning_event);

  universal_gnss::GnssDiagnosticEvent error_event;
  error_event.severity = universal_gnss::GnssDiagnosticSeverity::kError;
  error_event.category = universal_gnss::GnssDiagnosticCategory::kReceiver;
  error_event.code = "jamming_critical";
  error_event.message = "Critical jamming detected";
  error_event.timestamp_ns = 2500000001LL;
  summary.AddEvent(error_event);

  const auto array =
      universal_gnss_ros2::ToDiagnosticArrayMessage(summary, "universal_gnss", "receiver-2");

  ASSERT_EQ(array.status.size(), 3u);
  EXPECT_EQ(array.header.stamp.sec, 2);
  EXPECT_EQ(array.header.stamp.nanosec, 500000001u);

  const auto& summary_status = array.status.front();
  EXPECT_EQ(summary_status.name, "universal_gnss/summary");
  EXPECT_EQ(summary_status.level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
  const auto* overall = FindKeyValue(summary_status, "overall_severity");
  ASSERT_NE(overall, nullptr);
  EXPECT_EQ(overall->value, "error");

  const auto* fix_valid = FindKeyValue(summary_status, "fix_valid");
  ASSERT_NE(fix_valid, nullptr);
  EXPECT_EQ(fix_valid->value, "true");

  const auto* event_count = FindKeyValue(summary_status, "event_count");
  ASSERT_NE(event_count, nullptr);
  EXPECT_EQ(event_count->value, "2");

  EXPECT_EQ(array.status[1].name, "universal_gnss/rtcm_not_used");
  EXPECT_EQ(array.status[2].name, "universal_gnss/jamming_critical");
  EXPECT_EQ(array.status[2].level, diagnostic_msgs::msg::DiagnosticStatus::ERROR);
}

}  // namespace
