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

enum class ProfilePreviewStatus : std::uint8_t
{
  kOk = 0,
  kInvalidArgument = 1,
  kUnsupportedVendor = 2,
  kUnsupportedProfile = 3,
  kBuildError = 4,
};

struct ProfilePreviewOptions
{
  std::string vendor{};
  std::string profile{};
  std::optional<std::string> receiver_model{};
  bool persistent{false};
  std::optional<universal_gnss_driver::ReceiverAutoConfigSignalProfile> signal_profile{};
  std::optional<std::vector<std::uint8_t>> signal_group_override{};
  std::optional<universal_gnss_driver::ReceiverAutoConfigOutputPort> output_port{};
  std::optional<std::uint32_t> baud{};
  std::optional<double> rate_hz{};
};

struct ProfilePreviewCommand
{
  universal_gnss_driver::ReceiverCommand command{};
  std::size_t payload_bytes{0u};
  std::string description{};
};

struct ProfilePreviewSummary
{
  std::size_t commands_total{0u};
  std::size_t runtime_commands{0u};
  std::size_t persistent_commands{0u};
  std::size_t factory_reset_commands{0u};
};

struct ProfilePreviewResult
{
  ProfilePreviewStatus status{ProfilePreviewStatus::kOk};
  std::string vendor{};
  std::string receiver_family{};
  std::optional<std::string> receiver_model{};
  std::string profile{};
  bool persistent{false};
  std::optional<universal_gnss_driver::ReceiverAutoConfigSignalProfile> signal_profile{};
  std::optional<std::vector<std::uint8_t>> signal_group_override{};
  std::optional<universal_gnss_driver::ReceiverAutoConfigOutputPort> output_port{};
  std::optional<universal_gnss_driver::ReceiverAutoConfigOutputPort> resolved_output_port{};
  std::optional<std::uint32_t> baud{};
  std::optional<double> rate_hz{};
  std::vector<ProfilePreviewCommand> commands{};
  ProfilePreviewSummary summary{};
  std::vector<std::string> warnings{};
  std::string error_message{};
};

ProfilePreviewResult BuildProfilePreview(const ProfilePreviewOptions& options);

std::string DescribeProfilePreviewCommand(
    const universal_gnss_driver::ReceiverCommand& command);

std::string FormatProfilePreviewText(const ProfilePreviewResult& result,
                                     bool verbose = false);

std::string FormatProfilePreviewJson(const ProfilePreviewResult& result,
                                     bool verbose = false);

}  // namespace universal_gnss_tools
