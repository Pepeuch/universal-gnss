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

// Portable rover dynamic-motion selector. Vendor builders map this semantic
// choice to their concrete command language when the selected rover profile
// supports it.
enum class ReceiverAutoConfigRoverDynamicMode : std::uint8_t
{
  kUav = 0,
  kSurveyMow = 1,
  kRover = 2,
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
  std::optional<std::string> receiver_model{};
  ReceiverAutoConfigProfile requested_profile{ReceiverAutoConfigProfile::kRoverHighPrecision};
  ReceiverAutoConfigApplyMode apply_mode{ReceiverAutoConfigApplyMode::kDryRun};
  std::optional<ReceiverAutoConfigSignalProfile> signal_profile{};
  // Explicit Unicore CONFIG SIGNALGROUP override (for example {3, 6}).
  // When set it replaces the profile/signal_profile default. Unicore plans
  // validate only syntax/range for this override; model-specific documented
  // combinations remain hints/warnings rather than a hard allowlist. Ignored by
  // non-Unicore plans.
  std::optional<std::vector<std::uint8_t>> signal_group_override{};
  // Optional Unicore rover-profile overrides. When absent, the model-aware
  // profile owns the dynamic mode and receiver correction-age defaults.
  // Ignored by non-Unicore plans and by the zero-command runtime_only profile.
  std::optional<ReceiverAutoConfigRoverDynamicMode> rover_dynamic_mode_override{};
  std::optional<std::uint32_t> unicore_rtk_timeout_s_override{};
  std::optional<std::uint32_t> unicore_dgps_timeout_s_override{};
  std::optional<ReceiverAutoConfigOutputPort> output_port{};
  std::optional<std::uint32_t> config_baud{};
  std::optional<double> rate_hz{};
  // Known current transport baud sourced from an explicit runtime apply input
  // such as --baud when discovery_result is unavailable. This is used only to
  // avoid redundant runtime CONFIG COM1 writes; when absent, planning remains
  // conservative.
  std::optional<std::uint32_t> current_transport_baud{};
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
  std::optional<std::string> receiver_model{};
  std::optional<std::string> detected_device{};
  std::optional<std::string> detected_stable_id{};
  std::optional<std::uint32_t> detected_baud{};
  std::optional<std::string> detected_receiver_identity{};
  std::optional<std::string> detected_receiver_model{};
  std::optional<std::string> detected_receiver_firmware_version{};
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

ReceiverAutoConfigPlan BuildReceiverAutoConfigPlan(const ReceiverAutoConfigRequest& request);

ReceiverAutoConfigPlan BuildReceiverAutoConfigPlan(
    const ReceiverProbeResult& discovery_result,
    ReceiverAutoConfigProfile requested_profile,
    ReceiverAutoConfigApplyMode apply_mode,
    std::optional<std::uint32_t> config_baud = std::nullopt,
    std::optional<double> rate_hz = std::nullopt);

std::optional<ReceiverAutoConfigProfile> ParseReceiverAutoConfigProfile(std::string_view profile);
std::optional<ReceiverAutoConfigSignalProfile> ParseReceiverAutoConfigSignalProfile(
    std::string_view signal_profile);
std::optional<ReceiverAutoConfigRoverDynamicMode> ParseReceiverAutoConfigRoverDynamicMode(
    std::string_view rover_dynamic_mode);
// Parses an enabled Unicore correction-age window in whole seconds. Zero
// disables the corresponding receiver engine and is deliberately not an
// accepted timeout override.
std::optional<std::uint32_t> ParseUnicoreCorrectionAgeTimeout(std::string_view timeout_s);
// Parses a Unicore signal-group override such as "3 6", "3,6", or "3/6" into
// two group bytes. Returns nullopt on empty input, ambiguous collapsed input
// such as "36", non-numeric tokens, out-of-range values, or anything other
// than exactly two groups.
std::optional<std::vector<std::uint8_t>> ParseUnicoreSignalGroupOverride(
    std::string_view signal_group);
std::optional<ReceiverAutoConfigOutputPort> ParseReceiverAutoConfigOutputPort(
    std::string_view output_port);

const char* ToString(ReceiverAutoConfigProfile profile);
const char* ToString(ReceiverAutoConfigApplyMode apply_mode);
const char* ToString(ReceiverAutoConfigSignalProfile signal_profile);
const char* ToString(ReceiverAutoConfigRoverDynamicMode rover_dynamic_mode);
const char* ToString(ReceiverAutoConfigOutputPort output_port);
const char* ToString(ReceiverAutoConfigPlanStatus status);

}  // namespace universal_gnss_driver
