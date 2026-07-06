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

class ConfigApplyTransportHooks
{
public:
  virtual ~ConfigApplyTransportHooks() = default;

  virtual bool ProbeReceiverPath(const std::string& device_path,
                                 const std::vector<std::uint32_t>& baud_candidates,
                                 std::uint32_t read_timeout_ms,
                                 universal_gnss_driver::ReceiverProbeResult& probe_result,
                                 std::string& error_message) = 0;

  virtual bool ReopenTransport(universal_gnss_transport::ByteDuplex& transport,
                               const std::string& device_path,
                               std::uint32_t baud_rate,
                               std::uint32_t read_timeout_ms,
                               std::string& error_message) = 0;
};

enum class ConfigApplyStatus : std::uint8_t
{
  kOk = 0,
  kPartialSuccess = 1,
  kInvalidArgument = 2,
  kUnsupportedReceiver = 3,
  kUnsupportedVendor = 4,
  kUnsupportedProfile = 5,
  kBuildError = 6,
  kSafetyRejected = 7,
  kTransportUnavailable = 8,
  kReadFailed = 9,
  kDispatchFailed = 10,
  kRejected = 11,
  kTimedOut = 12,
  kApplicationFailed = 13,
};

struct ConfigApplyOptions
{
  universal_gnss_driver::ReceiverDetectedFamily receiver_family{
      universal_gnss_driver::ReceiverDetectedFamily::kUnknown};
  std::optional<universal_gnss_driver::ReceiverProbeResult> discovery_result{};
  universal_gnss_driver::ReceiverAutoConfigProfile profile{
      universal_gnss_driver::ReceiverAutoConfigProfile::kRoverHighPrecision};
  universal_gnss_driver::ReceiverAutoConfigApplyMode apply_mode{
      universal_gnss_driver::ReceiverAutoConfigApplyMode::kDryRun};
  std::optional<universal_gnss_driver::ReceiverAutoConfigSignalProfile> signal_profile{};
  std::optional<std::vector<std::uint8_t>> signal_group_override{};
  std::optional<std::string> receiver_model{};
  std::optional<universal_gnss_driver::ReceiverAutoConfigOutputPort> output_port{};
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
  std::size_t required_commands_failed{0u};
  std::size_t optional_commands_failed{0u};
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
                                     const ConfigApplyOptions& options,
                                     ConfigApplyTransportHooks* hooks = nullptr);

std::string FormatConfigApplyText(const ConfigApplyResult& result);

std::string FormatConfigApplyJson(const ConfigApplyResult& result);

}  // namespace universal_gnss_tools
