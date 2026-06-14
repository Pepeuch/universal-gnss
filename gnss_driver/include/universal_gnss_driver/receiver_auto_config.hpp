#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss_driver/receiver_capabilities.hpp"
#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_discovery.hpp"

namespace universal_gnss_driver
{

enum class ReceiverAutoConfigProfile : std::uint8_t
{
  kRuntimeOnly = 0,
  kRoverHighPrecision = 1,
  kRoverHighPrecisionDebug = 2,
  kFactoryReset = 3,
};

enum class ReceiverAutoConfigApplyMode : std::uint8_t
{
  kDryRun = 0,
  kRuntimeOnly = 1,
  kPersistent = 2,
};

enum class ReceiverAutoConfigSignalProfile : std::uint8_t
{
  kBalanced = 0,
  kHighPrecision = 1,
  kAllSignals = 2,
  kMinimal = 3,
  kCustom = 4,
};

enum class ReceiverAutoConfigOutputPort : std::uint8_t
{
  kUart1 = 0,
  kUart2 = 1,
  kUsb = 2,
  kAll = 3,
  kAuto = 4,
};

enum class ReceiverAutoConfigPlanStatus : std::uint8_t
{
  kOk = 0,
  kInvalidArgument = 1,
  kUnsupportedReceiver = 2,
  kUnsupportedProfile = 3,
  kUnsupportedApplyMode = 4,
  kBuildError = 5,
};

struct ReceiverAutoConfigRequest
{
  ReceiverDetectedFamily receiver_family{ReceiverDetectedFamily::kUnknown};
  std::optional<ReceiverProbeResult> discovery_result{};
  ReceiverAutoConfigProfile requested_profile{
      ReceiverAutoConfigProfile::kRoverHighPrecision};
  ReceiverAutoConfigApplyMode apply_mode{ReceiverAutoConfigApplyMode::kDryRun};
  std::optional<ReceiverAutoConfigSignalProfile> signal_profile{};
  std::optional<ReceiverAutoConfigOutputPort> output_port{};
  std::optional<std::uint32_t> config_baud{};
  std::optional<double> rate_hz{};
  std::optional<std::string> transport_device_path{};
};

struct ReceiverAutoConfigValidationSummary
{
  bool receiver_recognized{false};
  bool config_supported{false};
  bool profile_supported{false};
  bool apply_mode_supported{false};
  bool production_ready{false};
  bool ready_to_execute{false};
  std::size_t generated_command_count{0u};
  std::size_t runtime_command_count{0u};
  std::size_t persistent_command_count{0u};
  std::size_t factory_reset_command_count{0u};
};

struct ReceiverAutoConfigRollbackExpectation
{
  bool changes_are_temporary{false};
  bool operator_action_required{false};
  std::string summary{};
  std::string operator_action{};
};

struct ReceiverAutoConfigPlan
{
  ReceiverAutoConfigPlanStatus status{ReceiverAutoConfigPlanStatus::kOk};
  ReceiverAutoConfigRequest request{};
  ReceiverVendor vendor{ReceiverVendor::kUnknown};
  std::string receiver_family_name{};
  bool capabilities_known{false};
  ReceiverCapabilities capabilities{};
  std::optional<std::string> detected_device{};
  std::optional<std::string> detected_stable_id{};
  std::optional<std::uint32_t> detected_baud{};
  std::optional<ReceiverProbeConfidence> discovery_confidence{};
  std::optional<int> discovery_score{};
  std::optional<ReceiverAutoConfigOutputPort> resolved_output_port{};
  std::vector<ReceiverCommand> commands{};
  std::vector<std::string> warnings{};
  ReceiverAutoConfigRollbackExpectation rollback_expectation{};
  ReceiverAutoConfigValidationSummary validation{};
  std::string unsupported_reason{};
  std::string error_message{};
};

ReceiverAutoConfigPlan BuildReceiverAutoConfigPlan(
    const ReceiverAutoConfigRequest& request);

ReceiverAutoConfigPlan BuildReceiverAutoConfigPlan(
    const ReceiverProbeResult& discovery_result,
    ReceiverAutoConfigProfile requested_profile,
    ReceiverAutoConfigApplyMode apply_mode,
    std::optional<std::uint32_t> config_baud = std::nullopt,
    std::optional<double> rate_hz = std::nullopt);

std::optional<ReceiverAutoConfigProfile> ParseReceiverAutoConfigProfile(
    std::string_view profile);
std::optional<ReceiverAutoConfigSignalProfile> ParseReceiverAutoConfigSignalProfile(
    std::string_view signal_profile);
std::optional<ReceiverAutoConfigOutputPort> ParseReceiverAutoConfigOutputPort(
    std::string_view output_port);

const char* ToString(ReceiverAutoConfigProfile profile);
const char* ToString(ReceiverAutoConfigApplyMode apply_mode);
const char* ToString(ReceiverAutoConfigSignalProfile signal_profile);
const char* ToString(ReceiverAutoConfigOutputPort output_port);
const char* ToString(ReceiverAutoConfigPlanStatus status);

}  // namespace universal_gnss_driver
