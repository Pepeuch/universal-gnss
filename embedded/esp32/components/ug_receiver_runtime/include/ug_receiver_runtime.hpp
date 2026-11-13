// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss_driver/receiver_config_application.hpp"
#include "universal_gnss_driver/receiver_response_causality.hpp"
#include "universal_gnss_driver/receiver_session.hpp"
#include "universal_gnss_driver/ublox_response_router.hpp"
#include "universal_gnss_driver/unicore_response_router.hpp"
#include "universal_gnss_protocols/ubx_framer.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"
#include <array>

namespace ug::embedded
{
// Single actor owns UART reads, parsed state, commands and dispatch. Callers
// exchange snapshots/messages, never access the mutable session concurrently.
class Receiver
{
public:
  explicit Receiver(universal_gnss_transport::ByteSink& uart,
                    universal_gnss_driver::ReceiverSessionConfig config = {});
  bool Feed(const std::uint8_t* bytes, std::size_t size, std::int64_t receipt_ns);
  bool Start(std::vector<universal_gnss_driver::ReceiverCommand> commands, std::int64_t now_ns);
  void Step(std::int64_t now_ns);
  void ConfigureTelemetry(universal_gnss_driver::ReceiverSessionConfig config);
  void InvalidateObservations();  // Does not clear a command quarantine.
  const universal_gnss::GnssRuntimeState& state() const
  {
    return session_.current_state();
  }
  const universal_gnss_driver::ReceiverSessionMetrics& metrics() const
  {
    return session_.metrics();
  }
  const universal_gnss_driver::ReceiverConfigApplication& application() const
  {
    return application_;
  }
  const std::optional<universal_gnss_protocols::UbxNavSvinRecord>& survey() const
  {
    return survey_;
  }
  bool indeterminate() const
  {
    return application_.transaction_engine().session_indeterminate();
  }
  std::uint64_t incarnation() const
  {
    return incarnation_;
  }

private:
  void Note(const universal_gnss_driver::ReceiverConfigApplicationResult& result);
  universal_gnss_driver::ReceiverSession session_;
  universal_gnss_driver::ReceiverConfigApplication application_;
  universal_gnss_driver::ResponseCausalityFence fence_;
  universal_gnss_protocols::UbxFrameFramer ubx_{2048};
  universal_gnss_driver::UbloxResponseRouter ublox_;
  universal_gnss_driver::UnicoreResponseRouter unicore_;
  std::array<char, 1024> line_{};
  std::size_t line_size_{0};
  bool line_overflow_{false};
  std::optional<std::uint64_t> ubx_generation_{};
  std::uint64_t line_generation_{0}, incarnation_{1};
  std::optional<universal_gnss_protocols::UbxNavSvinRecord> survey_{};
  std::int64_t last_receipt_ns_{0};
};
}  // namespace ug::embedded
