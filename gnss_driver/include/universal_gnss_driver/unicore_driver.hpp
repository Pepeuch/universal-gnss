#pragma once

#include <string_view>
#include <vector>

#include "universal_gnss_driver/receiver_driver.hpp"
#include "universal_gnss_driver/unicore_model_profile.hpp"
#include "universal_gnss_driver/unicore_session.hpp"

namespace universal_gnss_driver
{

class UnicoreDriver : public ReceiverDriver
{
public:
  explicit UnicoreDriver(UnicoreSessionConfig session_config = {});

  UnicoreDriver(std::string_view receiver_model,
                UnicoreSessionConfig session_config = {});

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

  const UnicoreSession& session() const;

private:
  static const std::vector<ReceiverConfigProfileKind>& SupportedProfileKinds();

  ReceiverDriverProfileBuildResult BuildProfile(
      const ReceiverConfigProfileKind profile_kind,
      ReceiverCommandSafetyLevel safety_level) const;

  UnicoreSession session_;
  const UnicoreModelProfile* model_profile_{&ResolveUnicoreModelProfile()};
};

}  // namespace universal_gnss_driver
