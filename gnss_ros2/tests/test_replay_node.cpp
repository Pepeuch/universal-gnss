#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <future>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "rclcpp/executors/single_threaded_executor.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "universal_gnss_ros2/msg/gnss_status.hpp"
#include "universal_gnss_ros2/msg/rtcm_frame.hpp"
#include "universal_gnss_ros2/replay_node.hpp"

namespace
{

std::string TestdataPath(const std::string& relative_path)
{
#ifndef TESTDATA_DIR
#error "TESTDATA_DIR must be defined for gnss_ros2 replay tests"
#endif

  return std::string(TESTDATA_DIR) + "/" + relative_path;
}

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

void SpinExecutorUntil(rclcpp::executors::SingleThreadedExecutor& executor,
                       const std::function<bool()>& predicate,
                       const std::chrono::milliseconds timeout)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline)
  {
    if (predicate())
    {
      return;
    }

    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

bool CallStepService(rclcpp::executors::SingleThreadedExecutor& executor,
                     const rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr& client,
                     std::string& message)
{
  if (!client->wait_for_service(std::chrono::seconds(1)))
  {
    ADD_FAILURE() << "step service was not available";
    message.clear();
    return false;
  }

  auto request = std::make_shared<std_srvs::srv::Trigger::Request>();
  auto future = client->async_send_request(request);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (std::chrono::steady_clock::now() < deadline)
  {
    executor.spin_some();
    if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
      const auto response = future.get();
      message = response->message;
      return response->success;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  ADD_FAILURE() << "step service call timed out";
  message.clear();
  return false;
}

class ReplayNodeTest : public ::testing::Test
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

TEST_F(ReplayNodeTest, SteppedReplayAdvancesThroughServiceOnUbxFixture)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      {"input_path", TestdataPath("ubx/nav_pvt_sat_monrf.ubx")},
      {"replay_mode", "stepped"},
      {"publish_rtcm", false},
  });

  auto node = std::make_shared<universal_gnss_ros2::ReplayNode>(options);
  auto client_node = std::make_shared<rclcpp::Node>("replay_step_client");
  auto client =
      client_node->create_client<std_srvs::srv::Trigger>("/universal_gnss_replay/step");

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(client_node);

  ASSERT_TRUE(node->publishers_ready());
  ASSERT_TRUE(node->last_diagnostics_message().has_value());
  EXPECT_NE(
      FindDiagnosticStatusByName(
          *node->last_diagnostics_message(), "universal_gnss_replay/progress"),
      nullptr);

  std::string response_message;
  std::size_t safety_counter = 0u;
  while (!node->replay_complete() && safety_counter < 16u)
  {
    EXPECT_TRUE(CallStepService(executor, client, response_message));
    ++safety_counter;
  }

  EXPECT_TRUE(node->replay_complete());
  EXPECT_TRUE(node->has_runtime_state());
  ASSERT_TRUE(node->last_status_message().has_value());
  ASSERT_TRUE(node->last_fix_message().has_value());
  ASSERT_TRUE(node->last_diagnostics_message().has_value());
  EXPECT_TRUE(node->current_state().fix_valid);
  EXPECT_TRUE(std::isfinite(node->last_fix_message()->latitude));
  EXPECT_TRUE(std::isfinite(node->last_fix_message()->longitude));

  const auto* progress =
      FindDiagnosticStatusByName(*node->last_diagnostics_message(),
                                 "universal_gnss_replay/progress");
  ASSERT_NE(progress, nullptr);
  EXPECT_EQ(progress->message, "Replay complete");

  EXPECT_FALSE(CallStepService(executor, client, response_message));
  EXPECT_EQ(response_message, "replay already complete");
}

TEST_F(ReplayNodeTest, FastReplayPublishesUnicoreStateFromFixture)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      {"input_path", TestdataPath("unicore/basic_ascii.log")},
      {"replay_mode", "fast"},
      {"publish_rtcm", false},
      {"timer_poll_ms", 1},
  });

  auto node = std::make_shared<universal_gnss_ros2::ReplayNode>(options);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);

  SpinExecutorUntil(
      executor,
      [&]() { return node->replay_complete() && node->last_status_message().has_value(); },
      std::chrono::milliseconds(1000));

  EXPECT_TRUE(node->replay_complete());
  EXPECT_TRUE(node->has_runtime_state());
  ASSERT_TRUE(node->last_status_message().has_value());
  ASSERT_TRUE(node->last_fix_message().has_value());
  ASSERT_TRUE(node->last_diagnostics_message().has_value());
  EXPECT_TRUE(node->current_state().fix_valid);
  EXPECT_GT(node->last_status_message()->satellites_tracked, 0u);
  EXPECT_TRUE(std::isfinite(node->last_fix_message()->latitude));
  EXPECT_TRUE(std::isfinite(node->last_fix_message()->longitude));
}

TEST_F(ReplayNodeTest, WallTimeReplayPublishesNmeaStateFromFixture)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      {"input_path", TestdataPath("nmea/basic_fix.nmea")},
      {"replay_mode", "wall_time"},
      {"publish_rtcm", false},
      {"wall_time_scale", 1000.0},
      {"timer_poll_ms", 1},
      {"fallback_step_ms", 1},
  });

  auto node = std::make_shared<universal_gnss_ros2::ReplayNode>(options);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);

  SpinExecutorUntil(
      executor,
      [&]() { return node->replay_complete() && node->last_fix_message().has_value(); },
      std::chrono::milliseconds(1000));

  EXPECT_TRUE(node->replay_complete());
  EXPECT_TRUE(node->has_runtime_state());
  ASSERT_TRUE(node->last_status_message().has_value());
  ASSERT_TRUE(node->last_fix_message().has_value());
  EXPECT_TRUE(node->current_state().fix_valid);
  EXPECT_TRUE(std::isfinite(node->last_fix_message()->latitude));
  EXPECT_TRUE(std::isfinite(node->last_fix_message()->longitude));
}

TEST_F(ReplayNodeTest, FastReplayCanPublishRtcmFramesFromFixture)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides(std::vector<rclcpp::Parameter>{
      {"input_path", TestdataPath("rtcm/basic_msm.rtcm")},
      {"replay_mode", "fast"},
      {"publish_rtcm", true},
      {"timer_poll_ms", 1},
  });

  auto node = std::make_shared<universal_gnss_ros2::ReplayNode>(options);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);

  SpinExecutorUntil(
      executor,
      [&]() { return node->replay_complete() && node->last_rtcm_message().has_value(); },
      std::chrono::milliseconds(1000));

  EXPECT_TRUE(node->replay_complete());
  ASSERT_TRUE(node->last_rtcm_message().has_value());
  ASSERT_TRUE(node->last_diagnostics_message().has_value());
  EXPECT_FALSE(node->has_runtime_state());
  EXPECT_FALSE(node->last_rtcm_message()->data.empty());
  EXPECT_NE(node->last_rtcm_message()->message_type, 0u);
}

}  // namespace
