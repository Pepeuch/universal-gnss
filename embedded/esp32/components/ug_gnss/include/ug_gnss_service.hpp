// SPDX-License-Identifier: LGPL-3.0-only
#pragma once
#include "esp_err.h"
#include "ug_command_history.hpp"
#include "ug_satellite_reports.hpp"
#include "ug_telemetry_operation.hpp"
#include "universal_gnss_protocols/ubx_records.hpp"
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
  std::optional<universal_gnss_protocols::UbxNavSvinRecord> survey{};
  std::uint64_t uart_fifo_overflows{}, uart_ring_buffer_full{}, uart_last_fifo_seq{},
      uart_last_ring_full_seq{};
  std::int64_t uart_last_fifo_observed_ns{}, uart_last_ring_full_observed_ns{},
      uart_max_poll_gap_ns{};
};
esp_err_t StartGnssService();
bool QueueBaseOperation(const BaseOperation&);
bool QueueBaseDisableOperation(const BaseDisableOperation&);
bool QueueTelemetryOperation(const TelemetryOperation&);
bool QueueReceiverDetection();
bool QueueReceiverRestart();
bool ReceiverOperationPending();
// One coherent numeric sample under the receiver status lock. last_bytes_ns is the
// monotonic receipt time; zero means no bytes have been observed.
struct ReceiverIoStatus
{
  std::uint64_t bytes, uart_overflows, uart_line_errors, uart_event_saturations;
  std::uint64_t uart_read_failures, uart_drain_limits;
  std::int64_t last_bytes_ns;
  // Sequences order dequeued FIFO/ring faults; uart_overflows is their sum.
  std::uint64_t uart_fifo_overflows, uart_ring_buffer_full, uart_last_fifo_seq,
      uart_last_ring_full_seq;
  std::int64_t uart_last_fifo_observed_ns, uart_last_ring_full_observed_ns, uart_max_poll_gap_ns;
};
ReceiverIoStatus ReceiverIoSnapshot();
ReceiverStatus ReceiverSnapshot();
CommandHistorySnapshot ReceiverCommands();
struct ReceiverSatelliteReport
{
  SatelliteReport report{};
  std::uint64_t receiver_incarnation{};
};
ReceiverSatelliteReport ReceiverSatellites();
}  // namespace ug::embedded
