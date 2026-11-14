// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "universal_gnss_driver/receiver_config_application.hpp"
namespace universal_gnss_driver
{
// A software capture fence only. It cannot prove a physical prior-byte cutoff.
class ResponseCausalityFence
{
public:
  std::uint64_t BeginCapture()
  {
    return ++last_capture_generation_;
  }

  void NoteDispatch(const ReceiverConfigApplicationResult& application_result)
  {
    if (!application_result.engine_result.has_value())
    {
      return;
    }

    const auto status = application_result.engine_result->status;
    if (status == universal_gnss_driver::ReceiverCommandTransactionEngineStepStatus::kDispatched ||
        status ==
            universal_gnss_driver::ReceiverCommandTransactionEngineStepStatus::kRetryDispatched)
    {
      minimum_eligible_capture_generation_ = last_capture_generation_;
    }
  }

  bool IsEligible(const ReceiverCommandResponse& response) const
  {
    return response.capture_generation.has_value() &&
           *response.capture_generation > minimum_eligible_capture_generation_;
  }

private:
  std::uint64_t last_capture_generation_{0u};
  std::uint64_t minimum_eligible_capture_generation_{0u};
};

}  // namespace universal_gnss_driver
