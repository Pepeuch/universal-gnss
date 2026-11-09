#pragma once

#include <chrono>
#include <optional>

namespace universal_gnss_ntrip
{

// Local NTRIP arrival timing is distinct from receiver-reported correction age.
class NtripCorrectionArrivalAgeEstimator
{
public:
  using TimePoint = std::chrono::steady_clock::time_point;

  void Reset()
  {
    last_accepted_msm_time_.reset();
  }

  void ObserveAcceptedMsm(const TimePoint time)
  {
    last_accepted_msm_time_ = time;
  }

  std::optional<float> EstimateSeconds(const TimePoint now) const
  {
    if (!last_accepted_msm_time_.has_value() || now < *last_accepted_msm_time_)
    {
      return std::nullopt;
    }

    return std::chrono::duration<float>(now - *last_accepted_msm_time_).count();
  }

private:
  std::optional<TimePoint> last_accepted_msm_time_{};
};

}  // namespace universal_gnss_ntrip
