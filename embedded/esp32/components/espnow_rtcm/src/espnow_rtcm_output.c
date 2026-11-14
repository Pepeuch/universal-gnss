/* GPL-3.0-only; see espnow_rtcm_output.h for provenance. */
#include "espnow_rtcm_output.h"
#include "rtcm_filter.h"
espnow_rtcm_output_result_t
espnow_rtcm_output_frame(const espnow_rtcm_output_ops_t* ops, const uint8_t* frame, size_t size)
{
  espnow_rtcm_output_result_t result = {0};
  if (!ops || !ops->write || !ops->clock_ms || !ops->idle || !frame || !size ||
      size > RTCM3_MAX_FRAME_LEN)
    return result;
  const uint32_t start = ops->clock_ms(ops->context);
  // Iteration guard also terminates a faulty clock/yield implementation.
  for (unsigned attempts = 0; result.accepted < size && attempts < 4096; ++attempts)
  {
    const uint32_t elapsed = (uint32_t)(ops->clock_ms(ops->context) - start);
    if (elapsed >= 200)
      break;
    const uint32_t wait = 200 - elapsed < 20 ? 200 - elapsed : 20;
    const int written =
        ops->write(frame + result.accepted, size - result.accepted, wait, ops->context);
    if (written < 0 || (size_t)written > size - result.accepted)
      break;
    result.accepted += (size_t)written;
    if (!written)
      ops->idle(ops->context);
  }
  result.complete = result.accepted == size;
  return result;
}
