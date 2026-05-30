#pragma once

#include <string_view>
#include <vector>

#include "universal_gnss_driver/receiver_driver.hpp"
#include "universal_gnss_driver/ublox_session.hpp"

namespace universal_gnss_driver
{

class UbloxDriver : public ReceiverDriver
{
public:
  explicit UbloxDriver(UbloxSessionConfig session_config = {});

  ReceiverVendor vendor() const override;

  std::string_view family() const override;

  const ReceiverCapabilities& capabilities() const override;

  const std::vector<ReceiverConfigProfileKind>& supported_profiles() const override;

  const universal_gnss::GnssRuntimeState& current_state() const override;

  void FeedBytes(const std::uint8_t* data,
                 std::size_t size,
                 std::optional<std::int64_t> timestamp_ns = std::nullopt) override;

  void FeedString(std::string_view text,
                  std::optional<std::int64_t> timestamp_ns = std::nullopt) override;

  void Finalize() override;

  void Reset() override;

  ReceiverDriverProfileBuildResult BuildRoverProfile(
      ReceiverCommandSafetyLevel safety_level = ReceiverCommandSafetyLevel::kRuntime) const override;

  ReceiverDriverProfileBuildResult BuildDiagnosticsProfile(
      ReceiverCommandSafetyLevel safety_level = ReceiverCommandSafetyLevel::kRuntime) const override;

  ReceiverDriverProfileBuildResult BuildBaseProfile(
      ReceiverCommandSafetyLevel safety_level = ReceiverCommandSafetyLevel::kRuntime) const override;

  const UbloxSession& session() const;

private:
  static const ReceiverCapabilities& DriverCapabilities();

  static const std::vector<ReceiverConfigProfileKind>& SupportedProfileKinds();

  static ReceiverDriverProfileBuildResult BuildProfile(
      const ReceiverConfigProfileKind profile_kind,
      ReceiverCommandSafetyLevel safety_level);

  UbloxSession session_;
};

}  // namespace universal_gnss_driver
