#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_config_profile.hpp"
#include "universal_gnss_protocols/ubx_cfg_builder.hpp"

namespace universal_gnss_driver
{

enum class UbloxConfigProfileBuildStatus : std::uint8_t
{
  kOk = 0,
  kInvalidArgument = 1,
  kBuilderError = 2,
};

struct UbloxMessageRate
{
  std::uint32_t message_rate_key{0u};
  std::uint8_t rate{1u};
};

struct UbloxConstellationConfig
{
  universal_gnss_protocols::UbxCfgConstellation constellation{
      universal_gnss_protocols::UbxCfgConstellation::kGps};
  bool enabled{true};
};

struct UbloxPortConfig
{
  std::optional<std::uint32_t> uart1_baudrate{};
};

struct UbloxConfigProfile
{
  ReceiverConfigProfileKind config_kind{ReceiverConfigProfileKind::kRover};
  ReceiverCommandSafetyLevel safety_level{ReceiverCommandSafetyLevel::kRuntime};
  std::vector<universal_gnss_protocols::UbxCfgLayer> target_layers{
      universal_gnss_protocols::UbxCfgLayer::kRam};
  UbloxPortConfig port{};
  std::optional<double> measurement_rate_hz{};
  std::vector<UbloxMessageRate> enabled_messages{};
  std::vector<std::uint32_t> disabled_messages{};
  std::vector<UbloxConstellationConfig> constellations{};
};

struct UbloxConfigProfileBuildResult
{
  UbloxConfigProfileBuildStatus status{UbloxConfigProfileBuildStatus::kOk};
  std::vector<ReceiverCommand> commands{};
  std::string error_message{};
};

class UbloxConfigProfileBuilder
{
public:
  static UbloxConfigProfileBuildResult Build(const UbloxConfigProfile& profile);

  static UbloxConfigProfile BuildUbloxRoverProfile(
      ReceiverCommandSafetyLevel safety_level = ReceiverCommandSafetyLevel::kRuntime,
      std::vector<universal_gnss_protocols::UbxCfgLayer> layers = {
          universal_gnss_protocols::UbxCfgLayer::kRam});

  static UbloxConfigProfile BuildUbloxBaseProfile(
      ReceiverCommandSafetyLevel safety_level = ReceiverCommandSafetyLevel::kRuntime,
      std::vector<universal_gnss_protocols::UbxCfgLayer> layers = {
          universal_gnss_protocols::UbxCfgLayer::kRam});

  static UbloxConfigProfile BuildUbloxDiagnosticsProfile(
      ReceiverCommandSafetyLevel safety_level = ReceiverCommandSafetyLevel::kRuntime,
      std::vector<universal_gnss_protocols::UbxCfgLayer> layers = {
          universal_gnss_protocols::UbxCfgLayer::kRam});
};

}  // namespace universal_gnss_driver
