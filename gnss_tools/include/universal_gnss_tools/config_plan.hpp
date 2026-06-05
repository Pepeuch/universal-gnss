#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_auto_config.hpp"
#include "universal_gnss_driver/receiver_command.hpp"

namespace universal_gnss_tools
{

enum class ConfigPlanStatus : std::uint8_t
{
  kOk = 0,
  kInvalidArgument = 1,
  kUnsupportedReceiver = 2,
  kUnsupportedProfile = 3,
  kUnsupportedApplyMode = 4,
  kBuildError = 5,
};

struct ConfigPlanOptions
{
  std::string vendor{};
  std::string profile{};
  bool persistent{false};
  std::optional<std::uint32_t> baud{};
  std::optional<double> rate_hz{};
};

struct ConfigPlanCommand
{
  universal_gnss_driver::ReceiverCommand command{};
  std::size_t payload_bytes{0u};
  std::string description{};
  bool requires_explicit_safety_confirmation{false};
  bool dispatch_safe_without_confirmation{true};
};

struct ConfigPlanSummary
{
  std::size_t commands_total{0u};
  std::size_t runtime_commands{0u};
  std::size_t persistent_commands{0u};
  std::size_t factory_reset_commands{0u};
  std::size_t commands_requiring_confirmation{0u};
  bool requires_explicit_safety_confirmation{false};
};

struct ConfigPlanResult
{
  ConfigPlanStatus status{ConfigPlanStatus::kOk};
  std::string vendor{};
  std::string receiver_family{};
  std::string profile{};
  std::string apply_mode{};
  bool persistent{false};
  std::optional<std::uint32_t> baud{};
  std::optional<double> rate_hz{};
  bool dry_run{true};
  std::optional<std::string> detected_device{};
  std::optional<std::string> detected_stable_id{};
  std::optional<std::uint32_t> detected_baud{};
  std::optional<std::string> discovery_confidence{};
  std::optional<int> discovery_score{};
  std::vector<ConfigPlanCommand> commands{};
  ConfigPlanSummary summary{};
  bool receiver_recognized{false};
  bool config_supported{false};
  bool profile_supported{false};
  bool apply_mode_supported{false};
  bool production_ready{false};
  bool ready_to_execute{false};
  std::vector<std::string> warnings{};
  std::string rollback_expectation{};
  std::string unsupported_reason{};
  std::string error_message{};
};

ConfigPlanResult BuildConfigPlan(const ConfigPlanOptions& options);
ConfigPlanResult BuildConfigPlan(
    const universal_gnss_driver::ReceiverAutoConfigRequest& request);
ConfigPlanResult BuildConfigPlan(
    const universal_gnss_driver::ReceiverAutoConfigPlan& plan);

std::string FormatConfigPlanText(const ConfigPlanResult& result);

std::string FormatConfigPlanJson(const ConfigPlanResult& result);

}  // namespace universal_gnss_tools
