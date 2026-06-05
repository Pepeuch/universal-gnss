#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_auto_config.hpp"
#include "universal_gnss_driver/receiver_discovery.hpp"
#include "universal_gnss_tools/config_plan.hpp"

namespace universal_gnss_transport
{

class ByteDuplex;

}  // namespace universal_gnss_transport

namespace universal_gnss_tools
{

enum class ConfigApplyStatus : std::uint8_t
{
  kOk = 0,
  kInvalidArgument = 1,
  kUnsupportedReceiver = 2,
  kUnsupportedVendor = 3,
  kUnsupportedProfile = 4,
  kBuildError = 5,
  kSafetyRejected = 6,
  kTransportUnavailable = 7,
  kReadFailed = 8,
  kDispatchFailed = 9,
  kRejected = 10,
  kTimedOut = 11,
  kApplicationFailed = 12,
};

struct ConfigApplyOptions
{
  universal_gnss_driver::ReceiverDetectedFamily receiver_family{
      universal_gnss_driver::ReceiverDetectedFamily::kUnknown};
  std::optional<universal_gnss_driver::ReceiverProbeResult> discovery_result{};
  universal_gnss_driver::ReceiverAutoConfigProfile profile{
      universal_gnss_driver::ReceiverAutoConfigProfile::kRover};
  universal_gnss_driver::ReceiverAutoConfigApplyMode apply_mode{
      universal_gnss_driver::ReceiverAutoConfigApplyMode::kDryRun};
  std::optional<std::uint32_t> config_baud{};
  std::optional<double> rate_hz{};
  bool confirm{false};
  std::string device_path{};
  std::uint32_t transport_baud_rate{0u};
  std::uint32_t timeout_ms{1000u};
};

struct ConfigApplyExecutionSummary
{
  std::size_t commands_total{0u};
  std::size_t commands_completed{0u};
  std::size_t commands_failed{0u};
  std::size_t commands_retried{0u};
  std::size_t responses_applied{0u};
  std::string final_status{};
};

struct ConfigApplyResult
{
  ConfigApplyStatus status{ConfigApplyStatus::kOk};
  bool dry_run{true};
  bool execute_requested{false};
  bool executed{false};
  bool requires_runtime_confirmation{false};
  bool requires_persistent_confirmation{false};
  bool execution_confirmed{false};
  std::string device_path{};
  std::uint32_t transport_baud_rate{0u};
  std::uint32_t timeout_ms{0u};
  ConfigPlanResult plan{};
  ConfigApplyExecutionSummary execution_summary{};
  std::vector<std::string> progress_log{};
  std::string error_message{};
};

ConfigApplyResult PrepareConfigApply(const ConfigApplyOptions& options);

ConfigApplyResult ExecuteConfigApply(universal_gnss_transport::ByteDuplex& transport,
                                     const ConfigApplyOptions& options);

std::string FormatConfigApplyText(const ConfigApplyResult& result);

std::string FormatConfigApplyJson(const ConfigApplyResult& result);

}  // namespace universal_gnss_tools
