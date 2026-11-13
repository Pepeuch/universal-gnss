// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
#include "ug_base_operation.hpp"
namespace ug::embedded
{
struct ReceiverStatus
{
  bool present{}, model_verified{}, busy{}, quarantined{};
  std::string model{}, firmware{}, last_error{};
  std::uint64_t bytes{}, observations{}, command_operations{};
  std::uint64_t uart_overflows{}, uart_line_errors{}, uart_event_saturations{},
      uart_read_failures{}, uart_drain_limits{};
  std::int64_t last_bytes_ns{};
  unsigned operation_state{};
};
esp_err_t StartGnssService();
bool QueueBaseOperation(const BaseOperation&);
ReceiverStatus ReceiverSnapshot();
}  // namespace ug::embedded
