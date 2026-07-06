#include "universal_gnss_driver/unicore_model_profile.hpp"

#include <cctype>
#include <sstream>

namespace universal_gnss_driver
{

namespace
{

ReceiverCapabilities MakeUnicoreCapabilities(const bool baseline_capable,
                                             const bool signal_group_config_supported)
{
  ReceiverCapabilities capabilities;
  AddSupportedInputProtocol(capabilities, ReceiverProtocol::kRtcm3);
  AddSupportedInputProtocol(capabilities, ReceiverProtocol::kUnicoreAscii);
  AddSupportedInputProtocol(capabilities, ReceiverProtocol::kUnicoreBinary);
  AddSupportedOutputProtocol(capabilities, ReceiverProtocol::kNmea);
  AddSupportedOutputProtocol(capabilities, ReceiverProtocol::kRtcm3);
  AddSupportedOutputProtocol(capabilities, ReceiverProtocol::kUnicoreAscii);
  AddSupportedOutputProtocol(capabilities, ReceiverProtocol::kUnicoreBinary);
  AddReceiverFeature(capabilities, ReceiverFeature::kRtk);
  AddReceiverFeature(capabilities, ReceiverFeature::kPps);
  AddReceiverFeature(capabilities, ReceiverFeature::kSurveyIn);
  AddReceiverFeature(capabilities, ReceiverFeature::kBaseMode);
  AddReceiverFeature(capabilities, ReceiverFeature::kRoverMode);
  AddReceiverFeature(capabilities, ReceiverFeature::kAsciiCommandConfig);
  if (signal_group_config_supported)
  {
    AddReceiverFeature(capabilities, ReceiverFeature::kSignalGroups);
  }
  if (baseline_capable)
  {
    AddReceiverFeature(capabilities, ReceiverFeature::kHeading);
    AddReceiverFeature(capabilities, ReceiverFeature::kDualAntenna);
    AddReceiverFeature(capabilities, ReceiverFeature::kDualAntennaBaseline);
  }
  return capabilities;
}

const UnicoreModelProfile& GenericUnicoreProfile()
{
  static const UnicoreModelProfile profile{
      UnicoreModel::kUnknown,
      "UM98x",
      "unknown",
      "unicore_um98x_placeholder",
      true,
      MakeUnicoreCapabilities(false, false),
      false,
      "",
      {},
  };
  return profile;
}

const UnicoreModelProfile& Um960Profile()
{
  // The current repo sources confirm UM960 as a known single-antenna/non-baseline
  // model, but they do not document a portable CONFIG SIGNALGROUP mapping for
  // it. Keep the model selectable without guessing any signal-group behavior.
  static const UnicoreModelProfile profile{
      UnicoreModel::kUm960,
      "UM98x",
      "UM960",
      "unicore_um960",
      false,
      MakeUnicoreCapabilities(false, false),
      true,
      "",
      {},
  };
  return profile;
}

const UnicoreModelProfile& Um980Profile()
{
  static const UnicoreModelProfile profile{
      UnicoreModel::kUm980,
      "UM98x",
      "UM980",
      "unicore_um980",
      false,
      MakeUnicoreCapabilities(false, true),
      true,
      "Build7923+",
      {
          {{1u}, "documented default single-antenna signal group", false, false, false, false},
          {{2u},
           "documented single-antenna all-frequency signal group",
           false,
           false,
           false,
           false},
          {{8u}, "documented single-antenna 50 Hz signal group", false, false, false, false},
      },
  };
  return profile;
}

const UnicoreModelProfile& Um981Profile()
{
  // The current repo sources confirm UM981 as a known single-antenna/non-baseline
  // model, but they do not document a portable CONFIG SIGNALGROUP mapping for
  // it. Keep the model selectable without guessing any signal-group behavior.
  static const UnicoreModelProfile profile{
      UnicoreModel::kUm981,
      "UM98x",
      "UM981",
      "unicore_um981",
      false,
      MakeUnicoreCapabilities(false, false),
      false,
      "",
      {},
  };
  return profile;
}

const UnicoreModelProfile& Um982Profile()
{
  static const UnicoreModelProfile profile{
      UnicoreModel::kUm982,
      "UM98x",
      "UM982",
      "unicore_um982",
      false,
      MakeUnicoreCapabilities(true, true),
      true,
      "Build7650+",
      {
          {{4u, 5u}, "documented default dual-antenna signal group", true, false, false, false},
          {{3u, 6u}, "documented dual-antenna rover signal group", true, false, false, true},
          {{5u, 0u}, "documented low-power signal group", false, false, true, false},
          {{7u, 0u}, "documented base-mode-only signal group", false, true, false, false},
      },
  };
  return profile;
}

const UnicoreModelProfile& Ub9a0Profile()
{
  static const UnicoreModelProfile profile{
      UnicoreModel::kUb9a0,
      "UM98x",
      "UB9A0",
      "unicore_ub9a0",
      false,
      MakeUnicoreCapabilities(false, true),
      true,
      "",
      {
          {{2u}, "documented default single-antenna signal group", false, false, false, false},
          {{9u}, "documented single-antenna signal group", false, false, false, false},
      },
  };
  return profile;
}

}  // namespace

std::string NormalizeUnicoreModelName(const std::string_view model)
{
  std::string normalized;
  normalized.reserve(model.size());
  for (const unsigned char c : model)
  {
    if (std::isalnum(c) == 0)
    {
      continue;
    }

    normalized.push_back(static_cast<char>(std::toupper(c)));
  }

  return normalized;
}

std::optional<UnicoreModel> ParseUnicoreModel(const std::string_view model)
{
  const std::string normalized = NormalizeUnicoreModelName(model);
  if (normalized == "UM960")
  {
    return UnicoreModel::kUm960;
  }
  if (normalized == "UM980")
  {
    return UnicoreModel::kUm980;
  }
  if (normalized == "UM981")
  {
    return UnicoreModel::kUm981;
  }
  if (normalized == "UM982")
  {
    return UnicoreModel::kUm982;
  }
  if (normalized == "UB9A0")
  {
    return UnicoreModel::kUb9a0;
  }

  return std::nullopt;
}

const char* ToString(const UnicoreModel model)
{
  switch (model)
  {
    case UnicoreModel::kUm960:
      return "UM960";
    case UnicoreModel::kUm980:
      return "UM980";
    case UnicoreModel::kUm981:
      return "UM981";
    case UnicoreModel::kUm982:
      return "UM982";
    case UnicoreModel::kUb9a0:
      return "UB9A0";
    case UnicoreModel::kUnknown:
      break;
  }

  return "unknown";
}

const UnicoreModelProfile& ResolveUnicoreModelProfile(const std::optional<std::string_view> model)
{
  if (!model.has_value())
  {
    return GenericUnicoreProfile();
  }

  const auto parsed = ParseUnicoreModel(*model);
  if (!parsed.has_value())
  {
    return GenericUnicoreProfile();
  }

  switch (*parsed)
  {
    case UnicoreModel::kUm960:
      return Um960Profile();
    case UnicoreModel::kUm980:
      return Um980Profile();
    case UnicoreModel::kUm981:
      return Um981Profile();
    case UnicoreModel::kUm982:
      return Um982Profile();
    case UnicoreModel::kUb9a0:
      return Ub9a0Profile();
    case UnicoreModel::kUnknown:
      break;
  }

  return GenericUnicoreProfile();
}

ReceiverTargetSelector BuildUnicoreTargetSelector(const UnicoreModelProfile& profile)
{
  return ReceiverTargetSelector{
      ReceiverVendor::kUnicore,
      profile.family,
      profile.model,
      profile.profile_id,
  };
}

const UnicoreSignalGroupSelection* FindUnicoreSignalGroupSelection(
    const UnicoreModelProfile& profile, const std::vector<std::uint8_t>& groups)
{
  for (const auto& option : profile.signal_group_options)
  {
    if (option.groups == groups)
    {
      return &option;
    }
  }

  return nullptr;
}

const UnicoreSignalGroupSelection* FindUnicorePortableRoverSignalGroupSelection(
    const UnicoreModelProfile& profile)
{
  for (const auto& option : profile.signal_group_options)
  {
    if (option.portable_rover_default)
    {
      return &option;
    }
  }

  return nullptr;
}

bool SupportsUnicorePortableRoverSurveyMow(const UnicoreModelProfile& profile)
{
  return profile.supports_rover_survey_mow;
}

std::string DescribeUnicorePortableRoverSurveyMowSupport(const UnicoreModelProfile& profile)
{
  if (!profile.supports_rover_survey_mow)
  {
    return "unsupported";
  }

  if (profile.rover_survey_mow_min_build == nullptr ||
      profile.rover_survey_mow_min_build[0] == '\0')
  {
    return "documented supported";
  }

  return std::string("documented supported, ") + profile.rover_survey_mow_min_build;
}

std::string FormatUnicoreSignalGroupSelection(const std::vector<std::uint8_t>& groups)
{
  if (groups.empty())
  {
    return "none";
  }

  std::ostringstream stream;
  for (std::size_t index = 0u; index < groups.size(); ++index)
  {
    if (index != 0u)
    {
      stream << ' ';
    }
    stream << static_cast<unsigned int>(groups[index]);
  }
  return stream.str();
}

std::string DescribeUnicoreSupportedSignalGroups(const UnicoreModelProfile& profile)
{
  if (profile.signal_group_options.empty())
  {
    return "none";
  }

  std::ostringstream stream;
  for (std::size_t index = 0u; index < profile.signal_group_options.size(); ++index)
  {
    const auto& option = profile.signal_group_options[index];
    if (index != 0u)
    {
      stream << ", ";
    }
    stream << FormatUnicoreSignalGroupSelection(option.groups);
    if (option.base_mode_only)
    {
      stream << " (base mode only)";
    }
    else if (option.low_power_mode)
    {
      stream << " (low power)";
    }
    else if (option.portable_rover_default)
    {
      stream << " (portable rover default)";
    }
  }

  return stream.str();
}

}  // namespace universal_gnss_driver
