#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "universal_gnss/gnss_runtime_state.hpp"
#include "universal_gnss_driver/receiver_capabilities.hpp"
#include "universal_gnss_driver/receiver_command.hpp"
#include "universal_gnss_driver/receiver_config_profile.hpp"
#include "universal_gnss_driver/receiver_profile.hpp"

namespace universal_gnss_driver
{

enum class ReceiverDriverProfileBuildStatus : std::uint8_t
{
  kOk = 0,
  kUnsupportedProfile = 1,
  kUnsupportedSafetyLevel = 2,
  kBuildError = 3,
};

struct ReceiverDriverProfileBuildResult
{
  ReceiverDriverProfileBuildStatus status{ReceiverDriverProfileBuildStatus::kOk};
  ReceiverConfigProfileKind profile_kind{ReceiverConfigProfileKind::kRover};
  std::vector<ReceiverCommand> commands{};
  std::string error_message{};
};

class ReceiverDriver
{
public:
  virtual ~ReceiverDriver() = default;

  virtual ReceiverVendor vendor() const = 0;

  virtual std::string_view family() const = 0;

  virtual const ReceiverCapabilities& capabilities() const = 0;

  virtual const std::vector<ReceiverConfigProfileKind>& supported_profiles() const = 0;

  bool SupportsProfile(const ReceiverConfigProfileKind kind) const
  {
    for (const auto supported_kind : supported_profiles())
    {
      if (supported_kind == kind)
      {
        return true;
      }
    }

    return false;
  }

  virtual const universal_gnss::GnssRuntimeState& current_state() const = 0;

  virtual void FeedBytes(const std::uint8_t* data,
                         std::size_t size,
                         std::optional<std::int64_t> timestamp_ns = std::nullopt) = 0;

  void FeedBytes(const std::vector<std::uint8_t>& bytes,
                 const std::optional<std::int64_t> timestamp_ns = std::nullopt)
  {
    FeedBytes(bytes.data(), bytes.size(), timestamp_ns);
  }

  virtual void FeedString(std::string_view text,
                          std::optional<std::int64_t> timestamp_ns = std::nullopt) = 0;

  virtual void Finalize() = 0;

  virtual void Reset() = 0;

  virtual ReceiverDriverProfileBuildResult BuildRoverProfile(
      ReceiverCommandSafetyLevel safety_level = ReceiverCommandSafetyLevel::kRuntime) const = 0;

  virtual ReceiverDriverProfileBuildResult BuildDiagnosticsProfile(
      ReceiverCommandSafetyLevel safety_level = ReceiverCommandSafetyLevel::kRuntime) const = 0;

  virtual ReceiverDriverProfileBuildResult BuildBaseProfile(
      ReceiverCommandSafetyLevel safety_level = ReceiverCommandSafetyLevel::kRuntime) const
  {
    (void)safety_level;

    ReceiverDriverProfileBuildResult result;
    result.status = ReceiverDriverProfileBuildStatus::kUnsupportedProfile;
    result.profile_kind = ReceiverConfigProfileKind::kBase;
    result.error_message = "base profile generation is not supported by this driver";
    return result;
  }
};

}  // namespace universal_gnss_driver
