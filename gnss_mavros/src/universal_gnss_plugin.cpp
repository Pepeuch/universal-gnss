#include <cstdint>
#include <iomanip>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>

#include "mavros/mavros_uas.hpp"
#include "mavros/plugin.hpp"
#include "mavros/plugin_filter.hpp"
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "universal_gnss_mavros/mavlink_gnss_adapter.hpp"
#include "universal_gnss_ros2/gnss_status_adapter.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"
#include "universal_gnss_ros2/navsat_fix_adapter.hpp"

namespace universal_gnss_mavros {
namespace {

std::string NewIncarnationPrefix()
{
  std::random_device random;
  std::ostringstream value;
  value << std::hex << std::setfill('0');
  for (int index = 0; index < 4; ++index)
  {
    value << std::setw(8) << random();
  }
  return value.str();
}

std::optional<std::uint32_t> KnownAccuracy(const std::uint32_t value)
{
  return value > 0u && value != UINT32_MAX ? std::optional<std::uint32_t>(value) : std::nullopt;
}

template <typename Message> RawGpsObservation ConvertRawCommon(const Message& message)
{
  RawGpsObservation observation;
  observation.mavlink_time_usec = message.time_usec;
  observation.fix_type = message.fix_type;
  observation.latitude_deg_e7 = message.lat;
  observation.longitude_deg_e7 = message.lon;
  observation.altitude_msl_mm = message.alt;
  observation.hdop_centi = message.eph;
  observation.vdop_centi = message.epv;
  observation.ground_speed_cm_s = message.vel;
  observation.course_cdeg = message.cog;
  observation.satellites_visible = message.satellites_visible;
  observation.horizontal_accuracy_mm = KnownAccuracy(message.h_acc);
  observation.vertical_accuracy_mm = KnownAccuracy(message.v_acc);
  return observation;
}

template <typename Message> RtkObservation ConvertRtk(const Message& message)
{
  RtkObservation observation;
  observation.time_last_baseline_ms = message.time_last_baseline_ms;
  observation.rtk_receiver_id = message.rtk_receiver_id;
  observation.gps_week = message.wn;
  observation.gps_time_of_week_ms = message.tow;
  observation.health = message.rtk_health;
  observation.rate_hz = message.rtk_rate;
  observation.satellites_used = message.nsats;
  observation.baseline_coordinate_system = message.baseline_coords_type;
  observation.baseline_a_mm = message.baseline_a_mm;
  observation.baseline_b_mm = message.baseline_b_mm;
  observation.baseline_c_mm = message.baseline_c_mm;
  observation.accuracy = message.accuracy;
  observation.iar_num_hypotheses = message.iar_num_hypotheses;
  return observation;
}

} // namespace

class UniversalGnssPlugin : public mavros::plugin::Plugin
{
public:
  explicit UniversalGnssPlugin(mavros::plugin::UASPtr uas) : Plugin(uas, "universal_gnss")
  {
    const auto source_id_prefix =
        node->declare_parameter<std::string>("source_id_prefix", "mavlink:fcu");
    gps1_frame_id_ = node->declare_parameter<std::string>("gps1_frame_id", "gps1");
    gps2_frame_id_ = node->declare_parameter<std::string>("gps2_frame_id", "gps2");
    adapter_ = std::make_unique<MavlinkGnssAdapter>(
        AdapterConfig{source_id_prefix, NewIncarnationPrefix()});

    const auto qos = rclcpp::SensorDataQoS();
    gps1_status_publisher_ =
        node->create_publisher<universal_gnss_ros2::msg::GnssStatus>("~/gps1/status", qos);
    gps2_status_publisher_ =
        node->create_publisher<universal_gnss_ros2::msg::GnssStatus>("~/gps2/status", qos);
    gps1_fix_publisher_ = node->create_publisher<sensor_msgs::msg::NavSatFix>("~/gps1/fix", qos);
    gps2_fix_publisher_ = node->create_publisher<sensor_msgs::msg::NavSatFix>("~/gps2/fix", qos);

    enable_connection_cb();
  }

  Subscriptions get_subscriptions() override
  {
    return {
        make_handler(&UniversalGnssPlugin::HandleGpsRawInt),
        make_handler(&UniversalGnssPlugin::HandleGps2Raw),
        make_handler(&UniversalGnssPlugin::HandleGpsRtk),
        make_handler(&UniversalGnssPlugin::HandleGps2Rtk),
        make_handler(&UniversalGnssPlugin::HandleSystemTime),
    };
  }

private:
  void connection_cb(const bool connected) override
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (adapter_->OnConnectionChanged(connected))
    {
      PublishReceiver(Receiver::kGps1, false);
      PublishReceiver(Receiver::kGps2, false);
    }
  }

  void HandleGpsRawInt(const mavlink::mavlink_message_t* message [[maybe_unused]],
                       mavlink::common::msg::GPS_RAW_INT& gps,
                       mavros::plugin::filter::SystemAndOk filter [[maybe_unused]])
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    adapter_->HandleRawGps(Receiver::kGps1, ConvertRawCommon(gps), node->now().nanoseconds());
    PublishReceiver(Receiver::kGps1, true);
  }

  void HandleGps2Raw(const mavlink::mavlink_message_t* message [[maybe_unused]],
                     mavlink::common::msg::GPS2_RAW& gps,
                     mavros::plugin::filter::SystemAndOk filter [[maybe_unused]])
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    auto observation = ConvertRawCommon(gps);
    observation.supports_dgps_age = true;
    if (gps.dgps_age != UINT32_MAX)
    {
      observation.dgps_age_ms = gps.dgps_age;
    }
    adapter_->HandleRawGps(Receiver::kGps2, observation, node->now().nanoseconds());
    PublishReceiver(Receiver::kGps2, true);
  }

  void HandleGpsRtk(const mavlink::mavlink_message_t* message [[maybe_unused]],
                    mavlink::common::msg::GPS_RTK& rtk,
                    mavros::plugin::filter::SystemAndOk filter [[maybe_unused]])
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    adapter_->HandleRtk(Receiver::kGps1, ConvertRtk(rtk), node->now().nanoseconds());
    PublishReceiver(Receiver::kGps1, false);
  }

  void HandleGps2Rtk(const mavlink::mavlink_message_t* message [[maybe_unused]],
                     mavlink::common::msg::GPS2_RTK& rtk,
                     mavros::plugin::filter::SystemAndOk filter [[maybe_unused]])
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    adapter_->HandleRtk(Receiver::kGps2, ConvertRtk(rtk), node->now().nanoseconds());
    PublishReceiver(Receiver::kGps2, false);
  }

  void HandleSystemTime(const mavlink::mavlink_message_t* message [[maybe_unused]],
                        mavlink::common::msg::SYSTEM_TIME& system_time,
                        mavros::plugin::filter::SystemAndOk filter [[maybe_unused]])
  {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (adapter_->HandleSystemTime({system_time.time_unix_usec, system_time.time_boot_ms}))
    {
      PublishReceiver(Receiver::kGps1, false);
      PublishReceiver(Receiver::kGps2, false);
    }
  }

  void PublishReceiver(const Receiver receiver, const bool publish_fix)
  {
    const auto snapshot = adapter_->Snapshot(receiver);
    auto status = universal_gnss_ros2::ToGnssStatusMessage(snapshot.state);
    status.source_id = snapshot.source_id;
    status.source_incarnation = snapshot.source_incarnation;
    status.position_observation_sequence = snapshot.position_observation_sequence;

    const bool gps1 = receiver == Receiver::kGps1;
    (gps1 ? gps1_status_publisher_ : gps2_status_publisher_)->publish(status);
    if (!publish_fix || !snapshot.state.fix_valid || !snapshot.state.latitude_deg.has_value() ||
        !snapshot.state.longitude_deg.has_value())
    {
      return;
    }

    auto fix = universal_gnss_ros2::ToNavSatFixMessage(snapshot.state);
    fix.header.frame_id = gps1 ? gps1_frame_id_ : gps2_frame_id_;
    (gps1 ? gps1_fix_publisher_ : gps2_fix_publisher_)->publish(fix);
  }

  std::mutex callback_mutex_{};
  std::unique_ptr<MavlinkGnssAdapter> adapter_{};
  std::string gps1_frame_id_{};
  std::string gps2_frame_id_{};
  rclcpp::Publisher<universal_gnss_ros2::msg::GnssStatus>::SharedPtr gps1_status_publisher_{};
  rclcpp::Publisher<universal_gnss_ros2::msg::GnssStatus>::SharedPtr gps2_status_publisher_{};
  rclcpp::Publisher<sensor_msgs::msg::NavSatFix>::SharedPtr gps1_fix_publisher_{};
  rclcpp::Publisher<sensor_msgs::msg::NavSatFix>::SharedPtr gps2_fix_publisher_{};
};

} // namespace universal_gnss_mavros

#include "mavros/mavros_plugin_register_macro.hpp" // NOLINT
MAVROS_PLUGIN_REGISTER(universal_gnss_mavros::UniversalGnssPlugin)
