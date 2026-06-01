#pragma once

#include <string_view>
#include <vector>

#include "universal_gnss_driver/nmea_session.hpp"
#include "universal_gnss_driver/receiver_driver.hpp"

namespace universal_gnss_driver
{

class NmeaDriver : public ReceiverDriver
{
public:
  explicit NmeaDriver(NmeaSessionConfig session_config = {});

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

  const NmeaSession& session() const;

private:
  static const ReceiverCapabilities& DriverCapabilities();

  static const std::vector<ReceiverConfigProfileKind>& SupportedProfileKinds();

  static ReceiverDriverProfileBuildResult MakeUnsupportedProfileResult(
      ReceiverConfigProfileKind profile_kind);

  NmeaSession session_;
};

}  // namespace universal_gnss_driver
