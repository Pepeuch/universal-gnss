// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_gnss_service.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ug_configuration.hpp"
#include "ug_gateway_service.hpp"
#include "ug_idf_uart.hpp"
#include "ug_receiver_runtime.hpp"
#include "ug_services.hpp"
#include "ug_uart_drain.hpp"
#include "ug_wait.hpp"
#include "universal_gnss_driver/receiver_discovery.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <type_traits>
namespace ug::embedded
{
namespace
{
using namespace universal_gnss_driver;
using namespace universal_gnss_transport;
NativeGnssUart uart;
Receiver receiver(uart);
std::mutex status_guard;
ReceiverStatus status;
QueueHandle_t operations;
StaticQueue_t operations_control;
std::array<std::uint8_t, sizeof(BaseOperation)> operations_memory{};
std::atomic<bool> pending{false};
std::int64_t Now()
{
  return esp_timer_get_time() * 1000;
}
void Error(const std::string& error)
{
  std::lock_guard<std::mutex> lock(status_guard);
  status.last_error = error.substr(0, 192);
}
void Owner(void*)
{
  std::vector<std::uint8_t> probe;
  probe.reserve(4096);
  ReceiverModelQueryResult identity;
  ReceiverDetectedFamily family = ReceiverDetectedFamily::kUnknown;
  std::uint64_t epoch = 0;
  std::uint32_t baud = 0;
  int tx_pin = -2, rx_pin = -2;
  bool configured_enabled = false;
  std::string selector;
  bool enabled = false, local = false, query_done = false, transaction = false;
  std::int64_t probe_started = Now(), last_bytes = 0;
  std::uint64_t drain_limits = 0;
  std::int64_t last_survey_poll = 0;
  auto invalidate_input = [&] {
    receiver.InvalidateObservations();
    epoch = BeginCorrectionSource(0, local);
    identity = {};
    family = ReceiverDetectedFamily::kUnknown;
    query_done = false;
    probe.clear();
    probe_started = Now();
    last_bytes = 0;
    std::lock_guard<std::mutex> lock(status_guard);
    status.model_verified = false;
    status.model.clear();
    status.firmware.clear();
    status.survey.reset();
  };
  for (;;)
  {
    const auto settings = Configuration();
    if (baud != settings->gnss_baud || selector != settings->gnss_model ||
        configured_enabled != settings->gnss_enabled || tx_pin != settings->gnss_tx_pin ||
        rx_pin != settings->gnss_rx_pin)
    {
      BeginCorrectionSource(0, false);
      receiver.InvalidateObservations();
      PublishGnssSnapshot(
          receiver.state(),
          receiver.metrics().position_payload_freshness.last_position_observation_timestamp_ns);
      baud = settings->gnss_baud;
      selector = settings->gnss_model;
      configured_enabled = settings->gnss_enabled;
      tx_pin = settings->gnss_tx_pin;
      rx_pin = settings->gnss_rx_pin;
      ReceiverSessionConfig telemetry;
      if (selector == "NMEA")
        telemetry.kind = ReceiverSessionKind::kNmea;
      else if (selector == "ZED-F9P")
        telemetry.kind = ReceiverSessionKind::kUblox;
      else if (selector == "UM980" || selector == "UM981" || selector == "UM982")
        telemetry.kind = ReceiverSessionKind::kUnicore;
      receiver.ConfigureTelemetry(telemetry);
      uart.Close();
      enabled = configured_enabled && uart.Open(*settings, BoardCapabilities().gnss_pin_mask);
      if (configured_enabled && !enabled)
        Error("GNSS UART unavailable or board pins rejected");
      identity = {};
      {
        std::lock_guard<std::mutex> lock(status_guard);
        status.model_verified = false;
        status.model.clear();
        status.firmware.clear();
        status.survey.reset();
      }
      family = ReceiverDetectedFamily::kUnknown;
      probe.clear();
      query_done = false;
      probe_started = Now();
      last_bytes = 0;
      local = enabled && settings->local_input;
      epoch = BeginCorrectionSource(0, local);
    }
    if (local != (enabled && settings->local_input))
    {
      local = enabled && settings->local_input;
      epoch = BeginCorrectionSource(0, local);
    }
    if (enabled)
    {
      const auto drained = DrainUart(
          [](auto* data, auto size, bool wait) { return uart.ReadBatch(data, size, wait); },
          [&](const auto* data, auto size) {
            const auto received = Now();
            last_bytes = received;
            receiver.Feed(data, size, received);
            if (local)
              SubmitCorrectionBytes(0, epoch, data, size, received);
            if (!query_done && probe.size() < 4096)
              probe.insert(probe.end(), data, data + std::min(size, 4096 - probe.size()));
          });
      if (drained.budget_exhausted)
        ++drain_limits;
      if (uart.TakeFault() || drained.failed)
      {
        invalidate_input();
        Error("GNSS UART discontinuity; observations and pending corrections invalidated");
      }
    }
    const auto now = Now();
    if (enabled && !query_done && (probe.size() >= 4096 || now - probe_started >= 2000000000LL))
    {
      // Active queries are read-only vendor polls. No configured model string proves identity.
      auto candidate = MakeExplicitReceiverPortCandidate("board:uart1");
      const auto detected = AnalyzeReceiverProbeBytes(candidate, baud, probe);
      family = detected.detected_family;
      if (family == ReceiverDetectedFamily::kUnknown)
      {
        if (selector == "ZED-F9P")
          family = ReceiverDetectedFamily::kUblox;
        else if (selector == "UM980" || selector == "UM981" || selector == "UM982")
          family = ReceiverDetectedFamily::kUnicore;
      }
      BeginCorrectionSource(0, false);
      identity = QueryReceiverModel(family, uart, 1000, 4096);
      if (selector == "auto" && !identity.model_verified &&
          (family == ReceiverDetectedFamily::kUnknown || family == ReceiverDetectedFamily::kNmea))
      {
        identity = QueryReceiverModel(ReceiverDetectedFamily::kUblox, uart, 700, 4096);
        if (identity.model_verified)
          family = ReceiverDetectedFamily::kUblox;
        else
        {
          identity = QueryReceiverModel(ReceiverDetectedFamily::kUnicore, uart, 700, 4096);
          if (identity.model_verified)
            family = ReceiverDetectedFamily::kUnicore;
        }
      }
      if (selector != "auto" && selector != "NMEA" &&
          identity.identity.model.value_or("") != selector)
      {
        identity.model_verified = false;
        Error("verified model does not match configured selector");
      }
      if (uart.TakeFault())
      {
        invalidate_input();
        Error("GNSS UART discontinuity during identity query");
        continue;
      }
      epoch = BeginCorrectionSource(0, local);
      query_done = true;
      std::lock_guard<std::mutex> lock(status_guard);
      status.model_verified = identity.model_verified;
      status.model = identity.identity.model.value_or("");
      status.firmware = identity.identity.firmware_version.value_or("");
    }
    // Silence invalidates observations and queued corrections. It is not a trusted
    // receiver-side byte cutoff and never releases a command quarantine.
    if (last_bytes && now - last_bytes > 5000000000LL)
    {
      receiver.InvalidateObservations();
      epoch = BeginCorrectionSource(0, local);
      last_bytes = 0;
      identity = {};
      query_done = false;
      probe.clear();
      probe_started = now;
      std::lock_guard<std::mutex> lock(status_guard);
      status.model_verified = false;
      status.model.clear();
      status.firmware.clear();
      status.survey.reset();
    }
    BaseOperation operation;
    if (xQueueReceive(operations, &operation, 0) == pdTRUE)
    {
      if (!enabled || !identity.model_verified || ReceiverJournalBlocked() || transaction ||
          receiver.indeterminate())
      {
        Error("receiver identity unavailable, command busy or persistent quarantine");
        pending = false;
      }
      else
      {
        BaseConfigRequest request;
        request.vendor = family == ReceiverDetectedFamily::kUblox ? ReceiverVendor::kUblox
                                                                  : ReceiverVendor::kUnicore;
        const std::string model = identity.identity.model.value_or("");
        request.model = model;
        request.model_verified = true;
        request.mode = operation.mode;
        request.height_datum = operation.datum;
        request.latitude_deg = operation.latitude_deg;
        request.longitude_deg = operation.longitude_deg;
        request.height_m = operation.height_m;
        request.duration_s = operation.duration_s;
        request.accuracy_01mm = operation.accuracy_01mm;
        request.reuse_distance_m = operation.reuse_distance_m;
        request.rtcm_output = operation.rtcm_output;
        request.persist = operation.persist;
        request.confirm_persistent = operation.confirm_persistent;
        auto plan = BuildBaseConfigProfile(request);
        if (!plan.ok())
        {
          Error(plan.error);
          pending = false;
        }
        else if (!BeginReceiverJournal())
        {
          Error("receiver journal unavailable; nothing dispatched");
          pending = false;
        }
        else
        {
          transaction = receiver.Start(std::move(plan.commands), Now());
          if (!transaction)
          {
            Error("configuration dispatch failed; journal retained");
            pending = false;
          }
          else
          {
            std::lock_guard<std::mutex> lock(status_guard);
            ++status.command_operations;
            status.last_error.clear();
          }
        }
      }
    }
    receiver.Step(Now());
    const auto state = receiver.application().state();
    if (transaction && (state == ReceiverConfigApplicationState::kCompleted ||
                        state == ReceiverConfigApplicationState::kFailed))
    {
      if (state == ReceiverConfigApplicationState::kCompleted && !receiver.indeterminate())
      {
        if (!CompleteReceiverJournal())
          Error("configuration acknowledged; persistent journal could not be cleared");
      }
      else
        Error("configuration incomplete; persistent quarantine retained");
      transaction = false;
      pending = false;
    }
    // Poll NAV-SVIN only for a verified F9P, never while a mutation is pending
    // or quarantined. This read-only poll changes no receiver output settings.
    if (enabled && identity.model_verified && identity.identity.model.value_or("") == "ZED-F9P" &&
        !pending && !transaction && !ReceiverJournalBlocked() && !receiver.indeterminate() &&
        Now() - last_survey_poll >= 1000000000LL)
    {
      constexpr std::array<std::uint8_t, 8> poll{0xb5, 0x62, 0x01, 0x3b, 0, 0, 0x3c, 0xb5};
      const auto written = uart.Write(poll.data(), poll.size());
      if (written.bytes_written != poll.size())
        Error("NAV-SVIN poll write failed");
      last_survey_poll = Now();
    }
    PublishGnssSnapshot(
        receiver.state(),
        receiver.metrics().position_payload_freshness.last_position_observation_timestamp_ns);
    {
      std::lock_guard<std::mutex> lock(status_guard);
      status.present = enabled && last_bytes != 0;
      status.last_bytes_ns = last_bytes;
      status.bytes = receiver.metrics().bytes_seen;
      status.observations = receiver.metrics().runtime_observations;
      status.busy = pending;
      status.quarantined = ReceiverJournalBlocked() || receiver.indeterminate();
      status.operation_state = static_cast<unsigned>(state);
      status.uart_overflows = uart.overflows;
      status.uart_line_errors = uart.line_errors;
      status.uart_event_saturations = uart.event_saturations;
      status.uart_read_failures = uart.read_failures;
      status.uart_drain_limits = drain_limits;
      status.survey = identity.model_verified && identity.identity.model.value_or("") == "ZED-F9P"
                          ? receiver.survey()
                          : std::nullopt;
    }
    // At most 4096 bytes per turn, then give lower-priority services CPU time.
    vTaskDelay(1);
  }
}
}  // namespace
esp_err_t StartGnssService()
{
  static_assert(std::is_trivially_copyable_v<BaseOperation>);
  if (operations)
    return ESP_ERR_INVALID_STATE;
  operations =
      xQueueCreateStatic(1, sizeof(BaseOperation), operations_memory.data(), &operations_control);
  if (!operations || xTaskCreate(Owner, "ug_gnss", 16384, nullptr, 4, nullptr) != pdPASS)
    return ESP_ERR_NO_MEM;
  return ESP_OK;
}
bool QueueBaseOperation(const BaseOperation& operation)
{
  if (!operations || pending.exchange(true))
    return false;
  if (xQueueSend(operations, &operation, 0) == pdTRUE)
    return true;
  pending = false;
  return false;
}
ReceiverStatus ReceiverSnapshot()
{
  std::lock_guard<std::mutex> lock(status_guard);
  return status;
}
}  // namespace ug::embedded
