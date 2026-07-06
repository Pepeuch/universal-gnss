#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss_driver/receiver_capabilities.hpp"
#include "universal_gnss_driver/receiver_command.hpp"

namespace universal_gnss_driver
{

enum class UnicoreModel : std::uint8_t
{
  kUnknown = 0,
  kUm960 = 1,
  kUm980 = 2,
  kUm981 = 3,
  kUm982 = 4,
  kUb9a0 = 5,
};

struct UnicoreSignalGroupSelection
{
  std::vector<std::uint8_t> groups{};
  const char* description{""};
  bool baseline_only{false};
  bool base_mode_only{false};
  bool low_power_mode{false};
  bool portable_rover_default{false};
};

struct UnicoreModelProfile
{
  UnicoreModel model_id{UnicoreModel::kUnknown};
  const char* family{"UM98x"};
  const char* model{"unknown"};
  const char* profile_id{"unicore_um98x_placeholder"};
  bool placeholder{true};
  ReceiverCapabilities capabilities{};
  bool supports_rover_survey_mow{false};
  const char* rover_survey_mow_min_build{""};
  std::vector<UnicoreSignalGroupSelection> signal_group_options{};
};

std::string NormalizeUnicoreModelName(std::string_view model);

std::optional<UnicoreModel> ParseUnicoreModel(std::string_view model);

const char* ToString(UnicoreModel model);

const UnicoreModelProfile& ResolveUnicoreModelProfile(
    std::optional<std::string_view> model = std::nullopt);

ReceiverTargetSelector BuildUnicoreTargetSelector(const UnicoreModelProfile& profile);

const UnicoreSignalGroupSelection* FindUnicoreSignalGroupSelection(
    const UnicoreModelProfile& profile, const std::vector<std::uint8_t>& groups);

const UnicoreSignalGroupSelection* FindUnicorePortableRoverSignalGroupSelection(
    const UnicoreModelProfile& profile);

bool SupportsUnicorePortableRoverSurveyMow(const UnicoreModelProfile& profile);

std::string DescribeUnicorePortableRoverSurveyMowSupport(const UnicoreModelProfile& profile);

std::string FormatUnicoreSignalGroupSelection(const std::vector<std::uint8_t>& groups);

std::string DescribeUnicoreSupportedSignalGroups(const UnicoreModelProfile& profile);

}  // namespace universal_gnss_driver
