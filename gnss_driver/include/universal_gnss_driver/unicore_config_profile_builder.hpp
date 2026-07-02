#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_config_profile.hpp"
#include "universal_gnss_driver/unicore_model_profile.hpp"

namespace universal_gnss_driver
{

enum class UnicoreConfigProfileBuildStatus : std::uint8_t
{
  kOk = 0,
  kInvalidArgument = 1,
};

enum class UnicoreMode : std::uint8_t
{
  kUnspecified = 0,
  kRover = 1,
  kBase = 2,
  kSurvey = 3,
};

enum class UnicoreNmeaVersion : std::uint8_t
{
  kV410 = 0,
  kV411 = 1,
};

enum class UnicorePersistenceTarget : std::uint8_t
{
  kRuntimeOnly = 0,
  kSaveConfig = 1,
};

enum class UnicoreOutputMessageKind : std::uint8_t
{
  kGpgga = 0,
  kGpgsv = 1,
  kGpgst = 2,
  kPvtslna = 3,
  kBestnava = 4,
  kRtkstatusa = 5,
  kRtcmstatusa = 6,
  kSatsinfoa = 7,
};

struct UnicoreRtkReliability
{
  std::int32_t primary{0};
  std::int32_t secondary{0};
};

struct UnicoreSignalConfig
{
  std::vector<std::uint8_t> groups{};
};

struct UnicoreOutputMessageRate
{
  UnicoreOutputMessageKind message{UnicoreOutputMessageKind::kGpgga};
  std::optional<double> period_s{};
};

struct UnicoreConfigProfile
{
  ReceiverTargetSelector target{};
  ReceiverConfigProfileKind config_kind{ReceiverConfigProfileKind::kRover};
  bool factory_reset{false};
  UnicoreMode mode{UnicoreMode::kUnspecified};
  std::optional<std::uint32_t> com1_baud_rate{};
  std::optional<UnicoreNmeaVersion> nmea_version{};
  std::optional<std::uint32_t> rtk_timeout_s{};
  std::optional<std::uint32_t> dgps_timeout_s{};
  std::optional<UnicoreRtkReliability> rtk_reliability{};
  std::optional<UnicoreSignalConfig> signal_config{};
  bool clear_current_port_outputs{false};
  std::vector<UnicoreOutputMessageRate> output_messages{};
  UnicorePersistenceTarget persistence{UnicorePersistenceTarget::kRuntimeOnly};
};

struct UnicoreConfigProfileBuildResult
{
  UnicoreConfigProfileBuildStatus status{UnicoreConfigProfileBuildStatus::kOk};
  std::vector<ReceiverCommand> commands{};
  std::string error_message{};
};

class UnicoreConfigProfileBuilder
{
public:
  static UnicoreConfigProfileBuildResult Build(const UnicoreConfigProfile& profile);

  static UnicoreConfigProfile BuildUnicoreRoverProfile(
      UnicorePersistenceTarget persistence = UnicorePersistenceTarget::kRuntimeOnly);

  static UnicoreConfigProfile BuildUnicoreRoverProfile(
      const UnicoreModelProfile& model_profile,
      UnicorePersistenceTarget persistence = UnicorePersistenceTarget::kRuntimeOnly);

  static UnicoreConfigProfile BuildUnicoreDiagnosticsProfile(
      UnicorePersistenceTarget persistence = UnicorePersistenceTarget::kRuntimeOnly);

  static UnicoreConfigProfile BuildUnicoreDiagnosticsProfile(
      const UnicoreModelProfile& model_profile,
      UnicorePersistenceTarget persistence = UnicorePersistenceTarget::kRuntimeOnly);

  static UnicoreConfigProfile BuildUnicoreFactoryResetProfile();
};

}  // namespace universal_gnss_driver
