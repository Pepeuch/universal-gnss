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
#include "ug_supervisor.hpp"
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
ReceiverSatelliteReport satellite_snapshot;
QueueHandle_t operations;
StaticQueue_t operations_control;
std::array<std::uint8_t, sizeof(GnssOperation)> operations_memory{};
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
  WorkerHealth health(ServiceId::kGnss, 5000000000LL, true);
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
    local = local && !ReceiverJournalBlocked() && !receiver.indeterminate();
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
    satellite_snapshot = {};
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
        satellite_snapshot = {};
      }
      family = ReceiverDetectedFamily::kUnknown;
      probe.clear();
      query_done = false;
      probe_started = Now();
      last_bytes = 0;
      local = enabled && settings->local_input && !ReceiverJournalBlocked() &&
              !receiver.indeterminate();
      epoch = BeginCorrectionSource(0, local);
    }
    const bool route_local =
        enabled && settings->local_input && !ReceiverJournalBlocked() && !receiver.indeterminate();
    if (local != route_local)
    {
      local = route_local;
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
      // Discovery consumes UART bytes outside Feed. Discard parser/cache state
      // across that gap; this still cannot release a command quarantine.
      receiver.InvalidateObservations();
      PublishGnssSnapshot(receiver.state(), std::nullopt);
      {
        std::lock_guard<std::mutex> lock(status_guard);
        satellite_snapshot = {};
      }
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
        health.Progress();
        vTaskDelay(1);
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
      local = local && !ReceiverJournalBlocked() && !receiver.indeterminate();
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
      satellite_snapshot = {};
    }
    GnssOperation operation;
    if (xQueueReceive(operations, &operation, 0) == pdTRUE)
    {
      if (operation.kind == GnssOperation::Kind::kDetect)
      {
        if (!enabled || transaction)
          Error("GNSS UART unavailable or mutation in progress");
        else
        {
          // Read-only discovery is allowed while quarantined, but never clears
          // the application/journal or re-enables its local correction source.
          invalidate_input();
          if (!ReceiverJournalBlocked() && !receiver.indeterminate())
            Error("");
        }
        pending = false;
      }
      else if (!enabled || !identity.model_verified || ReceiverJournalBlocked() || transaction ||
               receiver.indeterminate())
      {
        Error("receiver identity unavailable, command busy or persistent quarantine");
        pending = false;
      }
      else
      {
        BaseConfigPlan plan;
        const std::string model = identity.identity.model.value_or("");
        if (operation.kind == GnssOperation::Kind::kTelemetry)
          plan = BuildTelemetryPlan(operation.telemetry, model, true);
        else if (operation.kind == GnssOperation::Kind::kBaseDisable)
        {
          BaseDisableRequest request;
          request.vendor = family == ReceiverDetectedFamily::kUblox ? ReceiverVendor::kUblox
                                                                    : ReceiverVendor::kUnicore;
          request.model = model;
          request.model_verified = true;
          request.uart_port = operation.disable_base.receiver_port;
          request.persist = operation.disable_base.persist;
          request.confirm_persistent = operation.disable_base.confirm_persistent;
          plan = BuildBaseDisableProfile(request);
        }
        else if (operation.kind == GnssOperation::Kind::kBase)
        {
          BaseConfigRequest request;
          request.vendor = family == ReceiverDetectedFamily::kUblox ? ReceiverVendor::kUblox
                                                                    : ReceiverVendor::kUnicore;
          request.model = model;
          request.model_verified = true;
          request.mode = operation.base.mode;
          request.height_datum = operation.base.datum;
          request.latitude_deg = operation.base.latitude_deg;
          request.longitude_deg = operation.base.longitude_deg;
          request.height_m = operation.base.height_m;
          request.duration_s = operation.base.duration_s;
          request.accuracy_01mm = operation.base.accuracy_01mm;
          request.reuse_distance_m = operation.base.reuse_distance_m;
          request.rtcm_output = operation.base.rtcm_output;
          request.uart_port = operation.base.receiver_port;
          request.persist = operation.base.persist;
          request.confirm_persistent = operation.base.confirm_persistent;
          plan = BuildBaseConfigProfile(request);
        }

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
          // Configuration may change the reference position/output/signals.
          // Suspend local corrections before dispatch; an incomplete journal
          // keeps this source disabled across a reboot as well.
          local = false;
          epoch = BeginCorrectionSource(0, false);
          receiver.InvalidateObservations();
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
      satellite_snapshot = {receiver.satellites(Now()), receiver.incarnation()};
      status.survey = identity.model_verified && identity.identity.model.value_or("") == "ZED-F9P"
                          ? receiver.survey()
                          : std::nullopt;
    }
    // At most 4096 bytes per turn, then give lower-priority services CPU time.
    health.Progress();
    vTaskDelay(1);
  }
}
}  // namespace
esp_err_t StartGnssService()
{
  static_assert(std::is_trivially_copyable_v<GnssOperation>);
  if (operations)
    return ESP_ERR_INVALID_STATE;
  operations =
      xQueueCreateStatic(1, sizeof(GnssOperation), operations_memory.data(), &operations_control);
  ExpectWorker(ServiceId::kGnss, 5000000000LL);
  if (!operations || xTaskCreate(Owner, "ug_gnss", 16384, nullptr, 4, nullptr) != pdPASS)
    return ESP_ERR_NO_MEM;
  return ESP_OK;
}
static bool QueueOperation(const GnssOperation& operation)
{
  if (!operations || pending.exchange(true))
    return false;
  if (xQueueSend(operations, &operation, 0) == pdTRUE)
    return true;
  pending = false;
  return false;
}
bool QueueBaseOperation(const BaseOperation& operation)
{
  GnssOperation request;
  request.base = operation;
  return QueueOperation(request);
}
bool QueueTelemetryOperation(const TelemetryOperation& operation)
{
  GnssOperation request;
  request.kind = GnssOperation::Kind::kTelemetry;
  request.telemetry = operation;
  return QueueOperation(request);
}
bool QueueBaseDisableOperation(const BaseDisableOperation& operation)
{
  GnssOperation request;
  request.kind = GnssOperation::Kind::kBaseDisable;
  request.disable_base = operation;
  return QueueOperation(request);
}
bool QueueReceiverDetection()
{
  GnssOperation request;
  request.kind = GnssOperation::Kind::kDetect;
  return QueueOperation(request);
}
ReceiverSatelliteReport ReceiverSatellites()
{
  std::lock_guard<std::mutex> lock(status_guard);
  return pending.load() ? ReceiverSatelliteReport{} : satellite_snapshot;
}
ReceiverStatus ReceiverSnapshot()
{
  std::lock_guard<std::mutex> lock(status_guard);
  return status;
}
}  // namespace ug::embedded
