#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
  kUnsupportedVendor = 2,
  kUnsupportedProfile = 3,
  kBuildError = 4,
  kSafetyRejected = 5,
  kTransportUnavailable = 6,
  kReadFailed = 7,
  kDispatchFailed = 8,
  kRejected = 9,
  kTimedOut = 10,
  kApplicationFailed = 11,
};

struct ConfigApplyOptions
{
  std::string vendor{};
  std::string profile{};
  bool persistent{false};
  std::optional<double> rate_hz{};
  bool execute{false};
  bool confirm_runtime{false};
  bool confirm_persistent{false};
  std::string port{};
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
  std::string port{};
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
