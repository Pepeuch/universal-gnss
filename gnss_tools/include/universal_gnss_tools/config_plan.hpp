#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_tools/profile_preview.hpp"

namespace universal_gnss_tools
{

using ConfigPlanStatus = ProfilePreviewStatus;

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
  bool persistent{false};
  std::optional<std::uint32_t> baud{};
  std::optional<double> rate_hz{};
  bool dry_run{true};
  std::vector<ConfigPlanCommand> commands{};
  ConfigPlanSummary summary{};
  std::string error_message{};
};

ConfigPlanResult BuildConfigPlan(const ConfigPlanOptions& options);

std::string FormatConfigPlanText(const ConfigPlanResult& result);

std::string FormatConfigPlanJson(const ConfigPlanResult& result);

}  // namespace universal_gnss_tools
