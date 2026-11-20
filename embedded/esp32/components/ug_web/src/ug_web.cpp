// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_web.hpp"
extern "C" {
#include "espnow_rtcm.h"
}
#include "esp_heap_caps.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ug_admin.hpp"
#include "ug_configuration.hpp"
#include "ug_credentials.hpp"
#include "ug_gateway_service.hpp"
#include "ug_http_encoding.hpp"
#include "ug_https_server_status.hpp"
#include "ug_json.hpp"
#include "ug_memory_diagnostics.hpp"
#include "ug_network_budget.hpp"
#include "ug_network_operations.hpp"
#include "ug_pairing_window.hpp"
#include "ug_portable_settings.hpp"
#include "ug_resource_policy.hpp"
#include "ug_s3_execution_policy.h"
#include "ug_services.hpp"
#include "ug_supervisor.hpp"
#include "ug_tls_bootstrap.hpp"
#include "ug_web_auth_platform.hpp"
#include "ug_wire_selftest.hpp"
#if UG_LOCAL_GNSS
#include "ug_gnss_service.hpp"
#include "universal_gnss_driver/unicore_model_profile.hpp"
#endif
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>
extern "C" httpd_ssl_config_t ug_https_defaults(void);
extern "C" httpd_config_t ug_http_defaults(void);
namespace ug::embedded
{
namespace
{
LoadedWebIdentity identity;
httpd_handle_t server;
PairingWindow pairing_window;
esp_err_t FinishHttpsStartup(HttpsStartupReason reason, std::int32_t sdk_error)
{
  const auto result = RecordHttpsStartupStatus(reason, sdk_error);
#if CONFIG_IDF_TARGET_ESP32S3
  if (reason != HttpsStartupReason::kReady)
    ESP_LOGE("UG",
             "HTTPS startup: %s (sdk_error=%ld)",
             HttpsStartupReasonName(reason),
             static_cast<long>(sdk_error));
#endif
  return result;
}
extern "C" {
extern const std::uint8_t index_start[] asm("_binary_ug_web_index_gz_start"),
    index_end[] asm("_binary_ug_web_index_gz_end");
extern const std::uint8_t js_start[] asm("_binary_ug_web_js_gz_start"),
    js_end[] asm("_binary_ug_web_js_gz_end");
extern const std::uint8_t css_start[] asm("_binary_ug_web_css_gz_start"),
    css_end[] asm("_binary_ug_web_css_gz_end");
}
void Headers(httpd_req_t* req)
{
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req,
                     "Content-Security-Policy",
                     "default-src 'self'; script-src 'self'; style-src 'self'; frame-ancestors "
                     "'none'; base-uri 'none'");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}
esp_err_t Fail(httpd_req_t* req, const char* status, const char* message)
{
  Headers(req);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  const auto result = httpd_resp_sendstr(req, message);
  // SDK would otherwise purge an arbitrarily large unread request body after an
  // early rejection. Returning an error closes the session before that purge.
  return status[0] == '4' || status[0] == '5' ? ESP_FAIL : result;
}
MemorySample CurrentMemory()
{
  multi_heap_info_t internal{}, external{};
  heap_caps_get_info(&internal, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  heap_caps_get_info(&external, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return {internal.total_free_bytes + internal.total_allocated_bytes,
          internal.total_free_bytes,
          internal.minimum_free_bytes,
          internal.largest_free_block,
          external.total_free_bytes + external.total_allocated_bytes,
          external.total_free_bytes};
}
esp_err_t ResourceBusy(httpd_req_t* req)
{
  httpd_resp_set_hdr(req, "Retry-After", "2");
  return Fail(req, "503 Service Unavailable", "memory pressure; retry later or use local console");
}
esp_err_t Resources(httpd_req_t* req)
{
  const auto memory = CurrentMemory();
  const auto pressure = AssessResources(memory);
  // Available even under pressure: no cJSON tree, variable body or heap buffer.
  std::array<char, 512> buffer{};
  const int length =
      std::snprintf(buffer.data(),
                    buffer.size(),
                    "{\"pressure\":\"%s\",\"internal_free\":%zu,\"internal_min\":%zu,"
                    "\"internal_largest\":%zu,\"internal_total\":%zu,\"psram_total\":%zu,"
                    "\"psram_free\":%zu,\"web_mutations_allowed\":%s,\"satellites_allowed\":%s}",
                    ResourcePressureName(pressure),
                    memory.internal_free,
                    memory.internal_min,
                    memory.internal_largest,
                    memory.internal_total,
                    memory.external_total,
                    memory.external_free,
                    AdmitWebWork(pressure, WebWork::kMutation) ? "true" : "false",
                    AdmitWebWork(pressure, WebWork::kDetailedDiagnostics) ? "true" : "false");
  if (length < 0 || static_cast<std::size_t>(length) >= buffer.size())
    return Fail(req, "503 Service Unavailable", "resource report unavailable");
  Headers(req);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buffer.data(), length);
}
std::string Bearer(httpd_req_t* req)
{
  const auto size = httpd_req_get_hdr_value_len(req, "Authorization");
  if (!size || size > 135)
    return {};
  std::array<char, 136> header{};
  if (httpd_req_get_hdr_value_str(req, "Authorization", header.data(), header.size()) != ESP_OK)
    return {};
  const std::string_view text(header.data(), size);
  if (text.size() < 7 || (text[0] != 'B' && text[0] != 'b') || (text[1] != 'E' && text[1] != 'e') ||
      (text[2] != 'A' && text[2] != 'a') || (text[3] != 'R' && text[3] != 'r') ||
      (text[4] != 'E' && text[4] != 'e') || (text[5] != 'R' && text[5] != 'r') || text[6] != ' ')
    return {};
  return std::string(text.substr(7));
}
bool Authenticated(httpd_req_t* req, bool allow_restricted = false)
{
#if CONFIG_IDF_TARGET_ESP32S3
  auto& auth = WebAuthentication();
  const auto token = Bearer(req);
  return auth.Authorized(token, esp_timer_get_time(), allow_restricted) ||
         (auth.LegacyAllowed() && AuthorizedBearer(std::string("Bearer ") + token,
                                                   Configuration()->admin_token_sha256,
                                                   Sha256));
#else
  (void)allow_restricted;
  return AuthorizedBearer(
      std::string("Bearer ") + Bearer(req), Configuration()->admin_token_sha256, Sha256);
#endif
}
bool Body(httpd_req_t* req, std::string& body)
{
  char type[48]{};
  if (req->content_len <= 0 || req->content_len > 8192 ||
      httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
      (std::strcmp(type, "application/json") != 0 &&
       std::strcmp(type, "application/json; charset=utf-8") != 0))
    return false;
  body.resize(req->content_len);
  std::size_t used = 0;
  const auto deadline = esp_timer_get_time() + 5000000;
  while (used < body.size())
  {
    if (esp_timer_get_time() > deadline)
      return false;
    const auto n = httpd_req_recv(req, body.data() + used, body.size() - used);
    if (n <= 0)
      return false;
    used += static_cast<std::size_t>(n);
  }
  return true;
}
esp_err_t Json(httpd_req_t* req, const cJSON* root)
{
  SampleWebMemory();
  char* text = root ? cJSON_PrintUnformatted(root) : nullptr;
  SampleWebMemory();
  if (!text || !WebJsonMemoryHealthy())
  {
    cJSON_free(text);
    return Fail(req, "503 Service Unavailable", "response memory budget unavailable");
  }
  Headers(req);
  httpd_resp_set_type(req, "application/json");
  const auto result = httpd_resp_sendstr(req, text);
  cJSON_free(text);
  return result;
}
esp_err_t NtripPasswordReply(httpd_req_t* req, std::string_view target)
{
  const auto config = Configuration();
  const auto* endpoint = FindNtripEndpoint(*config, BoardCapabilities(), target);
  if (!endpoint)
    return Fail(req, "404 Not Found", "NTRIP connection unavailable");
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  auto* password =
      root ? cJSON_AddStringToObject(root.get(), "password", endpoint->password.c_str()) : nullptr;
  // At most 128 bytes, worst case six JSON escape bytes each, plus envelope.
  std::array<char, 800> reply{};
  struct Clean
  {
    cJSON* password;
    std::array<char, 800>& reply;
    ~Clean()
    {
      if (password)
        Wipe(password->valuestring, std::strlen(password->valuestring));
      Wipe(reply.data(), reply.size());
    }
  } clean{password, reply};
  if (!password || !WebJsonMemoryHealthy() ||
      !cJSON_PrintPreallocated(root.get(), reply.data(), reply.size(), false))
    return Fail(req, "503 Service Unavailable", "response memory budget unavailable");
  Headers(req);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, reply.data());
}
esp_err_t Status(httpd_req_t* req)
{
  const auto net = NetworkStats();
  const auto gateway = GatewayStats();
  auto config = Configuration();
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  if (!root)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  cJSON_AddStringToObject(root.get(), "board", BoardCapabilities().board.c_str());
  cJSON_AddStringToObject(root.get(), "role", BoardCapabilities().role.c_str());
  cJSON_AddStringToObject(root.get(), "web_transport", WebTransportName());
  auto* firmware = cJSON_AddObjectToObject(root.get(), "firmware");
  if (!firmware)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  if (!cJSON_AddStringToObject(firmware, "ug_version", UG_FIRMWARE_VERSION) ||
      !cJSON_AddStringToObject(firmware, "source_sha", UG_SOURCE_SHA) ||
      !cJSON_AddStringToObject(firmware, "hardware_variant", UG_HARDWARE_VARIANT) ||
      !cJSON_AddStringToObject(firmware, "web_transport", WebTransportName()) ||
      !cJSON_AddStringToObject(firmware, "esp_idf", esp_get_idf_version()) ||
      !cJSON_AddBoolToObject(firmware, "modified", UG_SOURCE_MODIFIED))
    return Fail(req, "503 Service Unavailable", "firmware identity memory unavailable");
  cJSON_AddBoolToObject(root.get(), "configuration_writable", ConfigurationWritable());
#if CONFIG_IDF_TARGET_ESP32S3
  cJSON_AddBoolToObject(root.get(), "admin_configured", WebAdministratorConfigured());
#else
  cJSON_AddBoolToObject(root.get(), "admin_configured", !config->admin_token_sha256.empty());
#endif
  cJSON_AddNumberToObject(root.get(), "uptime_s", esp_timer_get_time() / 1000000);
  cJSON_AddNumberToObject(root.get(), "heap_free", esp_get_free_heap_size());
  cJSON_AddNumberToObject(root.get(), "heap_min", esp_get_minimum_free_heap_size());
  cJSON_AddNumberToObject(root.get(),
                          "largest_internal",
                          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  cJSON_AddBoolToObject(root.get(), "upstream_connected", net.upstream.active);
  cJSON_AddBoolToObject(root.get(), "upstream_flowing", net.upstream_flowing);
  cJSON_AddNumberToObject(root.get(), "rtcm_frames", gateway.routing.valid);
  cJSON_AddNumberToObject(root.get(), "rtcm_rejected", gateway.routing.rejected);
  cJSON_AddNumberToObject(root.get(),
                          "queue_drops",
                          gateway.ingress_dropped + gateway.destination_dropped +
                              gateway.routing.dropped_full);
  cJSON_AddNumberToObject(root.get(), "stale_drops", gateway.routing.dropped_stale);
  cJSON_AddBoolToObject(root.get(), "receiver_quarantined", ReceiverJournalBlocked());
#if UG_LOCAL_GNSS
  // Non-sensitive I/O health for qualification: no coordinates, commands or
  // credentials. Ages use the receiver owner's same monotonic clock domain.
  const auto receiver = ReceiverIoSnapshot();
  auto* io = cJSON_AddObjectToObject(root.get(), "gnss_io");
  if (!io)
    return Fail(req, "503 Service Unavailable", "GNSS I/O diagnostic memory unavailable");
  // Preserve full counter/timestamp precision. Observed times are dequeues in
  // the owner's monotonic clock, not hardware interrupt timestamps.
  auto exact = [io](const char* key, std::uint64_t value) {
    std::array<char, 24> text{};
    const int count =
        std::snprintf(text.data(), text.size(), "%llu", static_cast<unsigned long long>(value));
    return count > 0 && static_cast<std::size_t>(count) < text.size() &&
           cJSON_AddStringToObject(io, key, text.data());
  };
  const auto receipt_now = esp_timer_get_time() * 1000;
  const auto age_ms = receiver.last_bytes_ns > 0 && receipt_now >= receiver.last_bytes_ns
                          ? (receipt_now - receiver.last_bytes_ns) / 1000000
                          : -1;
  if (!cJSON_AddNumberToObject(io, "bytes", receiver.bytes) ||
      !cJSON_AddNumberToObject(io, "uart_overflows", receiver.uart_overflows) ||
      !cJSON_AddNumberToObject(io, "uart_line_errors", receiver.uart_line_errors) ||
      !cJSON_AddNumberToObject(io, "uart_event_saturations", receiver.uart_event_saturations) ||
      !cJSON_AddNumberToObject(io, "uart_read_failures", receiver.uart_read_failures) ||
      !cJSON_AddNumberToObject(io, "uart_drain_limits", receiver.uart_drain_limits) ||
      !cJSON_AddNumberToObject(io, "last_bytes_age_ms", age_ms) ||
      !exact("uart_fifo_overflows", receiver.uart_fifo_overflows) ||
      !exact("uart_ring_buffer_full", receiver.uart_ring_buffer_full) ||
      !exact("uart_last_fifo_seq", receiver.uart_last_fifo_seq) ||
      !exact("uart_last_ring_full_seq", receiver.uart_last_ring_full_seq) ||
      !exact("uart_last_fifo_observed_ns", receiver.uart_last_fifo_observed_ns) ||
      !exact("uart_last_ring_full_observed_ns", receiver.uart_last_ring_full_observed_ns) ||
      !exact("uart_max_poll_gap_ns", receiver.uart_max_poll_gap_ns))
    return Fail(req, "503 Service Unavailable", "GNSS I/O diagnostic memory unavailable");
#endif
  espnow_rtcm_stats_t radio;
  espnow_rtcm_get_stats(&radio);
  auto* r = cJSON_AddObjectToObject(root.get(), "espnow");
  if (!r)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  cJSON_AddBoolToObject(r, "started", radio.started);
  cJSON_AddBoolToObject(r, "configured_enabled", config->radio_enabled);
  cJSON_AddBoolToObject(r, "tx_quarantined", radio.tx_quarantined);
  cJSON_AddNumberToObject(r, "effective_channel", radio.effective_channel);
  cJSON_AddNumberToObject(r, "queued", radio.queued);
  cJSON_AddNumberToObject(r, "dropped", radio.dropped);
  cJSON_AddNumberToObject(r, "invalid", radio.invalid);
  cJSON_AddNumberToObject(r, "tx_frames", radio.tx_frames);
  cJSON_AddNumberToObject(r, "mac_ack_packets", radio.tx_packets_acknowledged);
  cJSON_AddNumberToObject(r, "tx_failures", radio.tx_failures);
#if CONFIG_IDF_TARGET_ESP32S3
  espnow_rtcm_queue_diagnostics_t queue{};
  espnow_rtcm_get_queue_diagnostics(&queue);
  auto* q = cJSON_AddObjectToObject(root.get(), "espnow_queue");
  if (!q)
    return Fail(req, "503 Service Unavailable", "radio diagnostic memory unavailable");
  if (!cJSON_AddBoolToObject(q, "available", queue.available) ||
      !cJSON_AddBoolToObject(q, "oldest_valid", queue.oldest_valid) ||
      !cJSON_AddNumberToObject(q, "capacity", queue.capacity) ||
      !cJSON_AddNumberToObject(q, "depth", queue.depth) ||
      !cJSON_AddNumberToObject(q, "peak", queue.peak) ||
      !cJSON_AddNumberToObject(q, "oldest_age_ms", queue.oldest_age_ms) ||
      !cJSON_AddNumberToObject(q, "max_dequeue_age_ms", queue.max_dequeue_age_ms) ||
      !cJSON_AddNumberToObject(q, "max_push_lock_hold_us", queue.max_push_lock_hold_us) ||
      !cJSON_AddNumberToObject(q, "max_pop_lock_hold_us", queue.max_pop_lock_hold_us) ||
      !cJSON_AddNumberToObject(q, "enter_delay_us_max", queue.enter_delay_us_max) ||
      !cJSON_AddNumberToObject(q, "worker_stack_min_bytes", queue.worker_stack_min_bytes) ||
      !cJSON_AddNumberToObject(q, "observed_core_mask", queue.observed_core_mask))
    return Fail(req, "503 Service Unavailable", "radio diagnostic memory unavailable");
#endif
  const auto services = ServiceHealths();
  const auto now = esp_timer_get_time() * 1000;
  cJSON_AddBoolToObject(
      root.get(), "degraded", ServicesDegraded(services, now) || !ConfigurationWritable());
  auto* health = cJSON_AddObjectToObject(root.get(), "services");
  if (!health)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  for (std::size_t i = 0; i < services.size(); ++i)
  {
    const auto& service = services[i];
    auto* value = cJSON_AddObjectToObject(health, ServiceName(static_cast<ServiceId>(i)));
    if (!value)
      return Fail(req, "503 Service Unavailable", "memory unavailable");
    cJSON_AddStringToObject(value, "state", ServiceStateName(service.state));
    cJSON_AddNumberToObject(value, "error", service.error);
    cJSON_AddBoolToObject(value, "progress_stale", ServiceStale(service, now));
    cJSON_AddBoolToObject(value, "worker_entered", service.entered);
    cJSON_AddBoolToObject(value, "watchdog_registered", service.watchdog_registered);
    cJSON_AddNumberToObject(value, "watchdog_error", service.watchdog_error);
    if (service.worker && now >= service.progress_ns)
      cJSON_AddNumberToObject(value, "progress_age_ms", (now - service.progress_ns) / 1000000);
    if (service.stack_sampled)
      cJSON_AddNumberToObject(value, "stack_min_bytes", service.stack_min_bytes);
  }
  // Public status contains no positions, credentials, raw receiver bytes or private MACs.
  return Json(req, root.get());
}
esp_err_t CapabilitiesReply(httpd_req_t* req)
{
  const auto& caps = BoardCapabilities();
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  if (!root)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  cJSON_AddStringToObject(root.get(), "web_transport", WebTransportName());
#if CONFIG_IDF_TARGET_ESP32S3
  cJSON_AddBoolToObject(root.get(), "password_authentication", true);
#else
  cJSON_AddBoolToObject(root.get(), "password_authentication", false);
#endif
  cJSON_AddBoolToObject(root.get(), "gnss", caps.local_gnss);
  cJSON_AddBoolToObject(root.get(), "caster", caps.caster);
  cJSON_AddBoolToObject(root.get(), "lora", caps.lora);
  auto* pins = cJSON_AddArrayToObject(root.get(), "gnss_allowed_pins");
  if (!pins)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  for (unsigned pin = 0; pin < 64; ++pin)
    if (caps.gnss_pin_mask & (UINT64_C(1) << pin))
      cJSON_AddItemToArray(pins, cJSON_CreateNumber(pin));
  cJSON_AddNumberToObject(root.get(), "publishers", caps.publisher_slots);
  const auto capacity = NetworkResourceCapacity();
  auto* limits = cJSON_AddObjectToObject(root.get(), "network_limits");
  if (!limits || !cJSON_AddNumberToObject(limits, "provisioned_sockets", capacity.sockets) ||
      !cJSON_AddNumberToObject(limits, "budgeted_sockets", capacity.required.sockets) ||
      !cJSON_AddNumberToObject(limits, "web_clients", kHttpsClientLimit) ||
      !cJSON_AddNumberToObject(limits, "https_clients", WebUsesHttps() ? kHttpsClientLimit : 0) ||
      !cJSON_AddNumberToObject(limits, "caster_clients", caps.caster ? kCasterClientLimit : 0) ||
      !cJSON_AddBoolToObject(limits, "budget_fits", NetworkBudgetFits(capacity)))
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  cJSON_AddBoolToObject(root.get(), "fixed_channel_espnow", true);
  cJSON_AddBoolToObject(root.get(), "radio_wifi_changes_require_reboot", true);
  return Json(req, root.get());
}
#if UG_LOCAL_GNSS
esp_err_t GnssCapabilitiesReply(httpd_req_t* req)
{
  const auto receiver = ReceiverSnapshot();
  const auto plans = InspectReceiverPlans(receiver.model, receiver.model_verified);
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  if (!root)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  cJSON_AddStringToObject(root.get(), "model", receiver.model.c_str());
  cJSON_AddBoolToObject(root.get(), "model_verified", receiver.model_verified);
  cJSON_AddBoolToObject(root.get(), "telemetry", plans.telemetry);
  cJSON_AddBoolToObject(root.get(), "fixed", plans.fixed);
  cJSON_AddBoolToObject(root.get(), "survey_in", plans.survey_in);
  cJSON_AddBoolToObject(root.get(), "time_optimization", plans.time_optimization);
  cJSON_AddBoolToObject(root.get(), "disable_base", plans.disable_base);
  cJSON_AddBoolToObject(root.get(), "restart", plans.restart);
  cJSON_AddBoolToObject(root.get(), "signal_groups_configurable", false);
  cJSON_AddStringToObject(root.get(),
                          "signal_groups_scope",
                          "catalogue_only; automatic persistence/reset not qualified");
  cJSON_AddStringToObject(root.get(), "qualification", "software_contract_only");
  auto* options = cJSON_AddArrayToObject(root.get(), "signal_groups");
  if (!options)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  if (receiver.model_verified && receiver.model.find("UM") == 0)
  {
    const auto& model = universal_gnss_driver::ResolveUnicoreModelProfile(receiver.model);
    for (const auto& group : model.signal_group_options)
    {
      if (group.base_mode_only || group.low_power_mode || group.groups.size() > 2)
        continue;
      auto* values = cJSON_CreateArray();
      if (!values)
        return Fail(req, "503 Service Unavailable", "memory unavailable");
      cJSON_AddItemToArray(options, values);
      for (auto id : group.groups)
        cJSON_AddItemToArray(values, cJSON_CreateNumber(id));
    }
  }
  return Json(req, root.get());
}
esp_err_t SatellitesReply(httpd_req_t* req)
{
  const auto snapshot = ReceiverSatellites();
  const auto& report = snapshot.report;
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  if (!root)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  const auto now = esp_timer_get_time() * 1000;
  const char* source = "unavailable";
  switch (report.protocol)
  {
    case SatelliteProtocol::kUbxNavSat:
      source = "UBX-NAV-SAT";
      break;
    case SatelliteProtocol::kUnicoreSatsInfo:
      source = "UNICORE-SATSINFOA";
      break;
    case SatelliteProtocol::kNmeaGsv:
      source = "NMEA-GSV";
      break;
    default:
      break;
  }
  cJSON_AddStringToObject(root.get(), "source", source);
  cJSON_AddStringToObject(
      root.get(), "freshness_basis", report.epoch_known ? "epoch_first_receipt" : "report_receipt");
  cJSON_AddBoolToObject(root.get(), "available", report.receipt_ns.has_value());
  cJSON_AddBoolToObject(root.get(), "stale", !SatelliteReportFresh(report, now));
  cJSON_AddBoolToObject(root.get(), "truncated", report.truncated);
  cJSON_AddBoolToObject(root.get(), "measurement_epoch_known", report.epoch_known);
  cJSON_AddNumberToObject(root.get(), "receiver_incarnation", snapshot.receiver_incarnation);
  cJSON_AddNumberToObject(root.get(), "reported_total", report.reported_total);
  if (report.receipt_ns && now >= *report.receipt_ns)
    cJSON_AddNumberToObject(root.get(), "report_age_ms", (now - *report.receipt_ns) / 1000000);
  if (report.protocol == SatelliteProtocol::kNmeaGsv)
    cJSON_AddStringToObject(root.get(), "talker", report.talker.data());
  if (report.signal_id)
    cJSON_AddNumberToObject(root.get(), "signal_id", *report.signal_id);
  if (report.epoch_known)
  {
    cJSON_AddNumberToObject(root.get(), "tow_ms", report.tow_ms);
    if (report.protocol == SatelliteProtocol::kUnicoreSatsInfo)
    {
      cJSON_AddNumberToObject(root.get(), "week", report.week);
      cJSON_AddNumberToObject(root.get(), "raw_time_reference", report.time_reference);
    }
  }
  auto* array = cJSON_AddArrayToObject(root.get(), "satellites");
  if (!array)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  for (std::size_t i = 0; i < report.count; ++i)
  {
    const auto& sat = report.entries[i];
    auto* value = cJSON_CreateObject();
    if (!value)
      return Fail(req, "503 Service Unavailable", "memory unavailable");
    cJSON_AddItemToArray(array, value);
    cJSON_AddNumberToObject(value, "svid", sat.id);
    if (report.protocol != SatelliteProtocol::kNmeaGsv)
      cJSON_AddNumberToObject(value, "raw_system_id", sat.raw_system_id);
    if (sat.flags & SatelliteEntry::kElevation)
      cJSON_AddNumberToObject(value, "elevation", sat.elevation);
    if (sat.flags & SatelliteEntry::kAzimuth)
      cJSON_AddNumberToObject(value, "azimuth", sat.azimuth);
    if (sat.flags & SatelliteEntry::kCn0)
      cJSON_AddNumberToObject(value, "cn0", sat.cn0);
    if (sat.flags & SatelliteEntry::kUsedKnown)
      cJSON_AddBoolToObject(value, "used", sat.flags & SatelliteEntry::kUsed);
    if (sat.flags & SatelliteEntry::kHealthKnown)
      cJSON_AddBoolToObject(value, "healthy", sat.flags & SatelliteEntry::kHealthy);
    if (report.protocol == SatelliteProtocol::kUnicoreSatsInfo)
    {
      cJSON_AddNumberToObject(value, "frequency_status", sat.frequency_status);
      cJSON_AddNumberToObject(value, "frequency_count", sat.frequency_count);
    }
  }
  return Json(req, root.get());
}
esp_err_t GnssReply(httpd_req_t* req)
{
  const auto gnss = ReceiverSnapshot();
  const auto observation = GnssObservation();
  const auto& state = observation.state;
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  if (!root)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  cJSON_AddBoolToObject(root.get(), "present", gnss.present);
  cJSON_AddBoolToObject(root.get(), "model_verified", gnss.model_verified);
  cJSON_AddStringToObject(root.get(), "model", gnss.model.c_str());
  cJSON_AddStringToObject(root.get(), "firmware", gnss.firmware.c_str());
  cJSON_AddBoolToObject(root.get(), "busy", gnss.busy);
  cJSON_AddBoolToObject(root.get(), "quarantined", gnss.quarantined);
  cJSON_AddStringToObject(root.get(), "last_error", gnss.last_error.c_str());
  cJSON_AddNumberToObject(root.get(), "operation_state", gnss.operation_state);
  const auto now = esp_timer_get_time() * 1000;
  const auto receipt = observation.position_receipt_ns;
  const bool stale = !receipt || now < *receipt || now - *receipt > 3000000000LL;
  cJSON_AddBoolToObject(root.get(), "position_stale", stale);
  if (receipt && now >= *receipt)
    cJSON_AddNumberToObject(root.get(), "position_observation_age_ms", (now - *receipt) / 1000000);
  cJSON_AddBoolToObject(root.get(), "fix_valid", state.fix_valid);
  cJSON_AddNumberToObject(root.get(), "fix_type", static_cast<unsigned>(state.fix_type));
  if (state.latitude_deg)
    cJSON_AddNumberToObject(root.get(), "latitude_deg", *state.latitude_deg);
  if (state.longitude_deg)
    cJSON_AddNumberToObject(root.get(), "longitude_deg", *state.longitude_deg);
  if (state.altitude_m)
    cJSON_AddNumberToObject(root.get(), "altitude_m", *state.altitude_m);
  if (state.satellites_used)
    cJSON_AddNumberToObject(root.get(), "satellites_used", *state.satellites_used);
  if (state.satellites_visible)
    cJSON_AddNumberToObject(root.get(), "satellites_visible", *state.satellites_visible);
  if (state.satellites_tracked)
    cJSON_AddNumberToObject(root.get(), "satellites_tracked", *state.satellites_tracked);
  if (state.rtk_mode)
    cJSON_AddNumberToObject(root.get(), "rtk_mode", static_cast<unsigned>(*state.rtk_mode));
  if (state.horizontal_accuracy_m)
    cJSON_AddNumberToObject(root.get(), "horizontal_accuracy_m", *state.horizontal_accuracy_m);
  if (state.vertical_accuracy_m)
    cJSON_AddNumberToObject(root.get(), "vertical_accuracy_m", *state.vertical_accuracy_m);
  if (state.hdop)
    cJSON_AddNumberToObject(root.get(), "hdop", *state.hdop);
  if (state.vdop)
    cJSON_AddNumberToObject(root.get(), "vdop", *state.vdop);
  if (state.correction_age_s)
    cJSON_AddNumberToObject(root.get(), "correction_age_s", *state.correction_age_s);
  if (state.mean_cn0_db_hz)
    cJSON_AddNumberToObject(root.get(), "mean_cn0_db_hz", *state.mean_cn0_db_hz);
  cJSON_AddNumberToObject(root.get(), "observations", gnss.observations);
  cJSON_AddNumberToObject(root.get(), "uart_overflows", gnss.uart_overflows);
  cJSON_AddNumberToObject(root.get(), "uart_line_errors", gnss.uart_line_errors);
  cJSON_AddNumberToObject(root.get(), "uart_event_saturations", gnss.uart_event_saturations);
  cJSON_AddNumberToObject(root.get(), "uart_read_failures", gnss.uart_read_failures);
  cJSON_AddNumberToObject(root.get(), "uart_drain_limits", gnss.uart_drain_limits);
  cJSON_AddNumberToObject(
      root.get(),
      "last_bytes_age_ms",
      gnss.last_bytes_ns ? (esp_timer_get_time() * 1000 - gnss.last_bytes_ns) / 1000000 : -1);
  cJSON_AddStringToObject(root.get(), "survey_completion", "not_inferred_from_ACK");
  if (gnss.survey)
  {
    const auto& survey = *gnss.survey;
    const bool fresh = survey.timestamp_ns && now >= *survey.timestamp_ns &&
                       now - *survey.timestamp_ns <= 3000000000LL;
    auto* value = cJSON_AddObjectToObject(root.get(), "survey_report");
    if (!value)
      return Fail(req, "503 Service Unavailable", "memory unavailable");
    cJSON_AddStringToObject(value, "source", "UBX-NAV-SVIN");
    cJSON_AddBoolToObject(value, "stale", !fresh);
    cJSON_AddBoolToObject(value, "active", survey.active);
    cJSON_AddBoolToObject(value, "position_valid", survey.valid);
    cJSON_AddBoolToObject(value, "fresh_valid_inactive", fresh && survey.valid && !survey.active);
    cJSON_AddBoolToObject(value, "confirms_current_request", false);
    cJSON_AddNumberToObject(value, "i_tow_ms", survey.i_tow_ms);
    cJSON_AddNumberToObject(value, "duration_s", survey.duration_s);
    cJSON_AddNumberToObject(value, "observations", survey.observations);
    cJSON_AddNumberToObject(value, "mean_accuracy_m", survey.mean_accuracy_01mm * 0.0001);
    cJSON_AddNumberToObject(value, "mean_ecef_x_m", survey.mean_x_01mm * 0.0001);
    cJSON_AddNumberToObject(value, "mean_ecef_y_m", survey.mean_y_01mm * 0.0001);
    cJSON_AddNumberToObject(value, "mean_ecef_z_m", survey.mean_z_01mm * 0.0001);
    if (survey.timestamp_ns && now >= *survey.timestamp_ns)
      cJSON_AddNumberToObject(value, "observation_age_ms", (now - *survey.timestamp_ns) / 1000000);
  }
  return Json(req, root.get());
}
#endif
bool OwnerJson(cJSON* parent,
               const char* name,
               const NetworkOwnerStats& stats,
               const char* connection_counter,
               const char* failure_counter,
               const char* expired_counter,
               bool report_bytes = true)
{
  auto* root = cJSON_AddObjectToObject(parent, name);
  if (!root || !cJSON_AddBoolToObject(root, "sample_current", stats.current) ||
      !cJSON_AddBoolToObject(root, "sampled", stats.sampled) ||
      !cJSON_AddBoolToObject(root, "active", stats.active) ||
      !cJSON_AddStringToObject(root,
                               "sample_configuration_revision",
                               std::to_string(stats.configuration_revision).c_str()) ||
      !cJSON_AddStringToObject(
          root, "sample_link_generation", std::to_string(stats.link_generation).c_str()) ||
      !cJSON_AddStringToObject(root, "frames", std::to_string(stats.frames).c_str()) ||
      !cJSON_AddStringToObject(root, connection_counter, std::to_string(stats.connections).c_str()))
    return false;
  if (report_bytes && !cJSON_AddStringToObject(root, "bytes", std::to_string(stats.bytes).c_str()))
    return false;
  if (failure_counter &&
      !cJSON_AddStringToObject(root, failure_counter, std::to_string(stats.failures).c_str()))
    return false;
  if (expired_counter &&
      !cJSON_AddStringToObject(root, expired_counter, std::to_string(stats.expired).c_str()))
    return false;
  const auto now = esp_timer_get_time() * 1000;
  return !stats.sampled || stats.receipt_ns < 0 || now < stats.receipt_ns ||
         cJSON_AddNumberToObject(root, "sample_age_ms", (now - stats.receipt_ns) / 1000000);
}
esp_err_t NtripRuntimeReply(httpd_req_t* req)
{
  const auto stats = NetworkStats();
  const auto config = Configuration();
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  if (!root || !cJSON_AddBoolToObject(root.get(), "network_ready", stats.network_ready) ||
      !cJSON_AddStringToObject(
          root.get(), "link_generation", std::to_string(stats.link_generation).c_str()) ||
      !cJSON_AddStringToObject(root.get(),
                               "configuration_revision",
                               std::to_string(stats.configuration_revision).c_str()) ||
      !cJSON_AddBoolToObject(root.get(), "upstream_flowing", stats.upstream_flowing) ||
      !cJSON_AddBoolToObject(root.get(), "local_input_selected", config->local_input) ||
      !OwnerJson(root.get(), "upstream", stats.upstream, "reconnect_count", nullptr, nullptr) ||
      !cJSON_AddNumberToObject(root.get(), "upstream_error", stats.upstream_error) ||
      !cJSON_AddStringToObject(
          root.get(), "gga_sent", std::to_string(stats.upstream_gga_sent).c_str()) ||
      !cJSON_AddStringToObject(
          root.get(), "gga_errors", std::to_string(stats.upstream_gga_errors).c_str()))
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  auto* publishers = cJSON_AddObjectToObject(root.get(), "publishers");
  if (!publishers)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  for (std::size_t i = 0; i < BoardCapabilities().publisher_slots; ++i)
    if (!OwnerJson(publishers,
                   std::to_string(i).c_str(),
                   stats.publishers[i],
                   "connections",
                   "failures",
                   "expired"))
      return Fail(req, "503 Service Unavailable", "memory unavailable");
  if (BoardCapabilities().caster && !OwnerJson(root.get(),
                                               "caster",
                                               stats.caster,
                                               "accepted_clients",
                                               "rejected_requests",
                                               "slow_client_disconnects",
                                               false))
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  return Json(req, root.get());
}
bool Confirmed(const cJSON* root, const char* expected)
{
  const auto* value = cJSON_GetObjectItemCaseSensitive(root, "confirm");
  return cJSON_IsString(value) && std::strcmp(value->valuestring, expected) == 0;
}
esp_err_t PairingReply(httpd_req_t* req)
{
  const auto config = Configuration();
  std::uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    return Fail(req, "503 Service Unavailable", "identity unavailable");
  char identity[18];
  std::snprintf(identity,
                sizeof(identity),
                "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0],
                mac[1],
                mac[2],
                mac[3],
                mac[4],
                mac[5]);
  espnow_rtcm_stats_t stats{};
  espnow_rtcm_get_stats(&stats);
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  if (!root ||
      !cJSON_AddStringToObject(root.get(), "identification", "manual_USB; no RF discovery") ||
      !cJSON_AddStringToObject(root.get(), "sta_mac", identity) ||
      !cJSON_AddBoolToObject(
          root.get(), "window_open", pairing_window.Active(esp_timer_get_time())) ||
      !cJSON_AddNumberToObject(root.get(), "peer_limit", 1) ||
      !cJSON_AddStringToObject(root.get(), "peer", config->radio_peer.c_str()) ||
      !cJSON_AddBoolToObject(root.get(), "key_set", !config->radio_key.empty()) ||
      !cJSON_AddBoolToObject(root.get(), "enabled", config->radio_enabled) ||
      !cJSON_AddBoolToObject(root.get(), "started", stats.started) ||
      !cJSON_AddNumberToObject(root.get(), "configured_channel", config->radio_channel) ||
      !cJSON_AddNumberToObject(root.get(), "effective_channel", stats.effective_channel) ||
      !cJSON_AddBoolToObject(root.get(),
                             "restart_required",
                             config->radio_enabled &&
                                 (stats.tx_quarantined ||
                                  !espnow_rtcm_configuration_matches(config->radio_peer.c_str(),
                                                                     config->radio_key.c_str(),
                                                                     config->radio_channel))) ||
      !cJSON_AddBoolToObject(root.get(), "tx_quarantined", stats.tx_quarantined) ||
      !cJSON_AddStringToObject(
          root.get(), "mac_ack_packets", std::to_string(stats.tx_packets_acknowledged).c_str()) ||
      !cJSON_AddBoolToObject(root.get(), "proves_rover_delivery", false))
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  return Json(req, root.get());
}
esp_err_t PairingMutation(httpd_req_t* req, std::string_view uri, const std::string& body)
{
  std::string error;
  auto root = ParseBoundedObject(body, error);
  if (!root)
    return Fail(req, "400 Bad Request", "invalid pairing request");
  if (uri == "/api/radio/pairing/window")
  {
    if (!OnlyKeys(root.get(), {"confirm"}) || !Confirmed(root.get(), "OPEN_LOCAL_PAIRING") ||
        !pairing_window.Open(esp_timer_get_time()))
      return Fail(req, "400 Bad Request", "explicit pairing window confirmation required");
    return PairingReply(req);
  }
  const bool remove = uri == "/api/radio/pairing/remove";
  if (remove)
  {
    if (!OnlyKeys(root.get(), {"confirm"}) || !Confirmed(root.get(), "UNPAIR_ESPNOW_ROVER"))
      return Fail(req, "400 Bad Request", "explicit unpair confirmation required");
  }
  else if (!pairing_window.Active(esp_timer_get_time()) ||
           !OnlyKeys(root.get(), {"confirm", "peer", "channel", "key"}) ||
           !Confirmed(root.get(), "PAIR_ESPNOW_ROVER"))
    return Fail(
        req, "409 Conflict", "open local pairing window and explicit confirmation required");
  JsonDocument patch(cJSON_CreateObject(), cJSON_Delete);
  auto* radio = patch ? cJSON_AddObjectToObject(patch.get(), "radio") : nullptr;
  if (!radio || !cJSON_AddBoolToObject(radio, "enabled", !remove))
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  if (remove)
  {
    if (!cJSON_AddStringToObject(radio, "peer", "") || !cJSON_AddStringToObject(radio, "key", ""))
      return Fail(req, "503 Service Unavailable", "memory unavailable");
  }
  else
  {
    for (const auto* key : {"peer", "channel", "key"})
    {
      auto* item = cJSON_GetObjectItemCaseSensitive(root.get(), key);
      auto* copy = item ? cJSON_Duplicate(item, true) : nullptr;
      if (!copy || !cJSON_AddItemToObject(radio, key, copy))
      {
        cJSON_Delete(copy);
        return Fail(req, "400 Bad Request", "incomplete pairing request");
      }
    }
  }
  char* raw = cJSON_PrintUnformatted(patch.get());
  if (!raw)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  const bool ok = PatchConfiguration(raw, error);
  cJSON_free(raw);
  if (!ok)
    return Fail(req, "409 Conflict", error.c_str());
  // Persisted change must not keep routing to the previous boot's recipient.
  espnow_rtcm_stop_until_reboot();
  pairing_window.Close();
  return PairingReply(req);
}
bool system_restart_pending = false;  // Single HTTP owner, shared by both restart routes.
void SystemRestart(void*)
{
  vTaskDelay(pdMS_TO_TICKS(750));
  esp_restart();
}
esp_err_t PairingRestartReply(httpd_req_t* req, const std::string& body)
{
  std::string error;
  auto root = ParseBoundedObject(body, error);
  const auto config = Configuration();
  if (!root || !OnlyKeys(root.get(), {"confirm"}) ||
      !Confirmed(root.get(), "RESTART_AFTER_PAIRING") || !config->radio_enabled ||
      config->radio_peer.empty() || config->radio_key.empty())
    return Fail(req, "409 Conflict", "confirmed saved pairing required");
  if (system_restart_pending)
    return Fail(req, "409 Conflict", "restart already pending");
  if (xTaskCreate(SystemRestart, "ug_pair_restart", 2048, nullptr, 2, nullptr) != pdPASS)
    return Fail(req, "503 Service Unavailable", "restart allocation failed");
  system_restart_pending = true;
  return Fail(req, "200 OK", "pairing restart scheduled");
}
esp_err_t SystemRestartReply(httpd_req_t* req, const std::string& body)
{
  std::string error;
  auto root = ParseBoundedObject(body, error);
  if (!root || !OnlyKeys(root.get(), {"confirm"}) || !Confirmed(root.get(), "REBOOT_ESP32"))
    return Fail(req, "400 Bad Request", "explicit REBOOT_ESP32 confirmation required");
#if UG_LOCAL_GNSS
  if (ReceiverOperationPending())
    return Fail(req, "409 Conflict", "GNSS operation pending; wait before restarting ESP32");
#endif
  if (system_restart_pending)
    return Fail(req, "409 Conflict", "restart already pending");
  if (xTaskCreate(SystemRestart, "ug_web_restart", 2048, nullptr, 2, nullptr) != pdPASS)
    return Fail(req, "503 Service Unavailable", "restart allocation failed");
  system_restart_pending = true;
  return Fail(req, "202 Accepted", "ESP32 restart scheduled; configuration retained");
}
esp_err_t NetworkHistoryReply(httpd_req_t* req)
{
  const auto snapshot = NetworkHistory();
  const auto now = esp_timer_get_time() * 1000;
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  if (!root ||
      !cJSON_AddStringToObject(root.get(), "scope", "volatile_ntrip_transitions_this_boot") ||
      !cJSON_AddBoolToObject(root.get(), "proves_correction_flow", false) ||
      !cJSON_AddStringToObject(
          root.get(), "overwritten", std::to_string(snapshot.overwritten).c_str()) ||
      !cJSON_AddStringToObject(root.get(), "rejected", std::to_string(snapshot.rejected).c_str()) ||
      !cJSON_AddStringToObject(root.get(), "coalesced", std::to_string(snapshot.coalesced).c_str()))
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  auto* list = cJSON_AddArrayToObject(root.get(), "events");
  if (!list)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  for (std::size_t i = 0; i < snapshot.count; ++i)
  {
    const auto& event = snapshot.events[i];
    auto* value = cJSON_CreateObject();
    if (!value)
      return Fail(req, "503 Service Unavailable", "memory unavailable");
    cJSON_AddItemToArray(list, value);
    if (!cJSON_AddStringToObject(value, "sequence", std::to_string(event.sequence).c_str()) ||
        !cJSON_AddStringToObject(value, "service", ServiceName(event.service)) ||
        !cJSON_AddStringToObject(value, "kind", NtripTraceName(event.kind)) ||
        !cJSON_AddStringToObject(value, "error", NtripTraceErrorName(event.error)) ||
        !cJSON_AddNumberToObject(value, "ug_client_error", event.client_error) ||
        !cJSON_AddStringToObject(value,
                                 "configuration_revision",
                                 std::to_string(event.configuration_revision).c_str()) ||
        !cJSON_AddStringToObject(
            value, "link_generation", std::to_string(event.link_generation).c_str()) ||
        !cJSON_AddStringToObject(value, "attempt", std::to_string(event.attempt).c_str()) ||
        !cJSON_AddNumberToObject(
            value, "age_ms", now >= event.receipt_ns ? (now - event.receipt_ns) / 1000000 : -1))
      return Fail(req, "503 Service Unavailable", "memory unavailable");
  }
  return Json(req, root.get());
}
esp_err_t EventReply(httpd_req_t* req)
{
  const auto snapshot = ServiceEvents();
  const auto now = esp_timer_get_time() * 1000;
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  if (!root)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  if (!cJSON_AddStringToObject(root.get(), "scope", "volatile_service_events_this_boot") ||
      !cJSON_AddStringToObject(
          root.get(), "overwritten", std::to_string(snapshot.overwritten).c_str()) ||
      !cJSON_AddStringToObject(root.get(), "rejected", std::to_string(snapshot.rejected).c_str()))
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  auto* list = cJSON_AddArrayToObject(root.get(), "events");
  if (!list)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  for (std::size_t i = 0; i < snapshot.count; ++i)
  {
    const auto& event = snapshot.events[i];
    auto* value = cJSON_CreateObject();
    if (!value)
      return Fail(req, "503 Service Unavailable", "memory unavailable");
    cJSON_AddItemToArray(list, value);
    if (!cJSON_AddStringToObject(value, "sequence", std::to_string(event.sequence).c_str()) ||
        !cJSON_AddStringToObject(value, "service", ServiceName(event.service)) ||
        !cJSON_AddStringToObject(value, "state", ServiceStateName(event.state)) ||
        !cJSON_AddStringToObject(value, "kind", ServiceEventName(event.kind)) ||
        !cJSON_AddNumberToObject(value, "error", event.error) ||
        !cJSON_AddBoolToObject(value, "clock_valid", now >= event.receipt_ns) ||
        (now >= event.receipt_ns &&
         !cJSON_AddNumberToObject(value, "age_ms", (now - event.receipt_ns) / 1000000)))
      return Fail(req, "503 Service Unavailable", "memory unavailable");
  }
  return Json(req, root.get());
}
#if UG_LOCAL_GNSS
esp_err_t CommandHistoryReply(httpd_req_t* req)
{
  const auto snapshot = ReceiverCommands();
  const auto now = esp_timer_get_time() * 1000;
  JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
  if (!root ||
      !cJSON_AddStringToObject(root.get(), "scope", "volatile_gnss_command_results_this_boot") ||
      !cJSON_AddStringToObject(root.get(),
                               "current_receiver_incarnation",
                               std::to_string(snapshot.current_receiver_incarnation).c_str()) ||
      !cJSON_AddStringToObject(
          root.get(), "overwritten", std::to_string(snapshot.overwritten).c_str()) ||
      !cJSON_AddStringToObject(root.get(), "rejected", std::to_string(snapshot.rejected).c_str()))
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  auto* list = cJSON_AddArrayToObject(root.get(), "events");
  if (!list)
    return Fail(req, "503 Service Unavailable", "memory unavailable");
  for (std::size_t i = 0; i < snapshot.count; ++i)
  {
    const auto& event = snapshot.events[i];
    auto* value = cJSON_CreateObject();
    if (!value)
      return Fail(req, "503 Service Unavailable", "memory unavailable");
    cJSON_AddItemToArray(list, value);
    if (!cJSON_AddStringToObject(value, "sequence", std::to_string(event.sequence).c_str()) ||
        !cJSON_AddStringToObject(value, "plan", std::to_string(event.plan).c_str()) ||
        !cJSON_AddStringToObject(
            value, "receiver_incarnation", std::to_string(event.receiver_incarnation).c_str()) ||
        !cJSON_AddStringToObject(value, "purpose", CommandPurposeName(event.purpose)) ||
        !cJSON_AddNumberToObject(value, "application_index", event.application_index) ||
        !cJSON_AddNumberToObject(value, "state", static_cast<unsigned>(event.state)) ||
        (event.engine_status_present &&
         !cJSON_AddNumberToObject(
             value, "engine_status", static_cast<unsigned>(event.engine_status))) ||
        !cJSON_AddBoolToObject(value, "started", event.started) ||
        !cJSON_AddBoolToObject(value, "finished", event.finished) ||
        !cJSON_AddBoolToObject(value, "succeeded", event.succeeded) ||
        !cJSON_AddBoolToObject(value, "failed", event.failed) ||
        !cJSON_AddBoolToObject(value, "response_applied", event.response_applied) ||
        !cJSON_AddBoolToObject(value, "retry", event.retry) ||
        !cJSON_AddBoolToObject(value, "indeterminate", event.indeterminate) ||
        !cJSON_AddBoolToObject(value, "failure_ignored", event.failure_ignored) ||
        !cJSON_AddBoolToObject(
            value, "clock_valid", event.clock_valid && now >= event.receipt_ns) ||
        (event.clock_valid && now >= event.receipt_ns &&
         !cJSON_AddNumberToObject(value, "age_ms", (now - event.receipt_ns) / 1000000)))
      return Fail(req, "503 Service Unavailable", "memory unavailable");
  }
  return Json(req, root.get());
}
#endif
void ObserveWebCore()
{
#if CONFIG_IDF_TARGET_ESP32S3 && defined(ESP_PLATFORM)
  static std::atomic<unsigned> observed{};
  const auto core = xPortGetCoreID();
  const unsigned bit = 1u << core;
  if (!(observed.fetch_or(bit, std::memory_order_relaxed) & bit))
    ESP_LOGI("UG_CPU",
             "role=web profile=%d configured_core=%d observed_core=%d",
             UG_S3_CPU_PROFILE,
             ug_s3_worker_core(UG_S3_WORKER_WEB),
             core);
#endif
}
esp_err_t Get(httpd_req_t* req)
{
  ObserveWebCore();
  WebMemoryScope memory(req->uri);
  if (req->content_len)
    return Fail(req, "400 Bad Request", "GET body is not supported");
  const std::string_view uri(req->uri);
  if (uri == "/api/resources")
    return Resources(req);
  const auto work = uri == "/api/gnss/satellites" || uri == "/api/diagnostics/events" ||
                            uri == "/api/gnss/commands" || uri == "/api/ntrip/history"
                        ? WebWork::kDetailedDiagnostics
                        : WebWork::kSnapshot;
  if (!AdmitWebWork(AssessResources(CurrentMemory()), work))
    return ResourceBusy(req);
  if (uri == "/api/status" || uri == "/status")
    return Status(req);
  if (uri == "/api/capabilities")
    return CapabilitiesReply(req);
  if (uri == "/api/auth/session")
    return Authenticated(req, true) ? Fail(req, "200 OK", "session active")
                                    : Fail(req, "401 Unauthorized", "session invalid");
  if (!Authenticated(req))
    return Fail(req, "401 Unauthorized", "administrator bearer token required");
  constexpr std::string_view password_prefix = "/api/ntrip/password/";
  if (uri.substr(0, password_prefix.size()) == password_prefix)
    return NtripPasswordReply(req, uri.substr(password_prefix.size()));
  if (uri == "/api/diagnostics/events")
    return EventReply(req);
  if (uri == "/api/ntrip/runtime")
    return NtripRuntimeReply(req);
  if (uri == "/api/ntrip/history")
    return NetworkHistoryReply(req);
  if (uri == "/api/radio/pairing")
    return PairingReply(req);
  if (uri == "/api/config/history")
  {
    const auto history = ConfigurationBanks();
    JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
    if (!root)
      return Fail(req, "503 Service Unavailable", "memory unavailable");
    cJSON_AddStringToObject(root.get(), "generation", std::to_string(history.generation).c_str());
    cJSON_AddStringToObject(
        root.get(), "previous_generation", std::to_string(history.previous_generation).c_str());
    cJSON_AddBoolToObject(root.get(), "previous_available", history.previous_available);
    cJSON_AddBoolToObject(root.get(), "writable", ConfigurationWritable());
    return Json(req, root.get());
  }
#if UG_LOCAL_GNSS
  if (uri == "/api/config/portable")
  {
    std::string json, error;
    if (!ExportPortableSettings(*Configuration(), BoardCapabilities(), json, error))
      return Fail(req, "409 Conflict", error.c_str());
    Headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.data(), json.size());
  }
#endif
  if (uri == "/api/config" || uri == "/config" || uri == "/api/ntrip")
  {
    std::string json;
    if (!EncodeSettings(*Configuration(), json))
      return Fail(req, "503 Service Unavailable", "configuration unavailable");
    Headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json.data(), json.size());
  }
#if UG_LOCAL_GNSS
  if (uri == "/api/gnss/commands")
    return CommandHistoryReply(req);
  if (uri == "/api/gnss/capabilities")
    return GnssCapabilitiesReply(req);
  if (uri == "/api/gnss/satellites")
    return SatellitesReply(req);
  if (uri == "/api/gnss/status" || uri == "/api/gnss/diagnostics" || uri == "/api/gnss/base/status")
    return GnssReply(req);
#endif
  return Fail(req, "404 Not Found", "API operation unavailable in this profile");
}
#if CONFIG_IDF_TARGET_ESP32S3
esp_err_t AuthenticationPost(httpd_req_t* req)
{
  if (!AdmitWebWork(AssessResources(CurrentMemory()), WebWork::kMutation))
    return ResourceBusy(req);
  // A separate tight bound applies before JSON allocation. Request buffers and
  // cJSON string values are wiped on all exits, including malformed requests.
  std::string body, error;
  if (req->content_len > 512 || !Body(req, body))
  {
    Wipe(body.data(), body.size());
    return Fail(req, "400 Bad Request", "bounded credentials required");
  }
  auto root = ParseBoundedObject(body, error);
  Wipe(body.data(), body.size());
  struct Clean
  {
    cJSON* root;
    ~Clean()
    {
      if (root)
        for (auto* v = root->child; v; v = v->next)
          if (cJSON_IsString(v))
            Wipe(v->valuestring, std::strlen(v->valuestring));
    }
  } clean{root.get()};
  if (!root)
    return Fail(req, "400 Bad Request", "invalid credentials request");
  const auto text = [&](const char* name) -> std::string_view {
    auto* v = cJSON_GetObjectItemCaseSensitive(root.get(), name);
    return cJSON_IsString(v) ? std::string_view(v->valuestring) : std::string_view{};
  };
  auto& auth = WebAuthentication();
  const auto now = esp_timer_get_time();
  const std::string_view uri(req->uri);
  if (uri == "/api/auth/login")
  {
    if (!OnlyKeys(root.get(), {"username", "password"}))
      return Fail(req, "400 Bad Request", "invalid login request");
    std::array<char, 65> token{};
    bool restricted = true;
    if (!auth.Login(text("username"), text("password"), now, token, restricted))
      return Fail(req, "401 Unauthorized", "login refused; wait before retrying");
    std::array<char, 192> reply{};
    const int n = std::snprintf(
        reply.data(),
        reply.size(),
        "{\"token\":\"%s\",\"password_change_required\":%s,\"expires_in_s\":600,\"sliding\":true}",
        token.data(),
        restricted ? "true" : "false");
    Wipe(token.data(), token.size());
    Headers(req);
    httpd_resp_set_type(req, "application/json");
    const auto result = httpd_resp_send(req, reply.data(), n);
    Wipe(reply.data(), reply.size());
    return result;
  }
  const auto token = Bearer(req);
  if (uri == "/api/auth/logout")
  {
    if (!OnlyKeys(root.get(), {}))
      return Fail(req, "400 Bad Request", "invalid logout request");
    auth.Logout(token);
    return Fail(req, "200 OK", "session closed");
  }
  if (!OnlyKeys(root.get(), {"current_password", "new_password", "confirm"}) ||
      !Confirmed(root.get(), "CHANGE_PASSWORD") ||
      !auth.Change(token, text("current_password"), text("new_password"), now))
    return Fail(req, "403 Forbidden", "password change refused; session or credentials invalid");
  return Fail(req, "200 OK", "password changed; all Web sessions revoked; reconnect");
}
#endif
esp_err_t Post(httpd_req_t* req)
{
  ObserveWebCore();
  WebMemoryScope memory(req->uri);
#if CONFIG_IDF_TARGET_ESP32S3
  if (std::string_view(req->uri) == "/api/auth/login" ||
      std::string_view(req->uri) == "/api/auth/password" ||
      std::string_view(req->uri) == "/api/auth/logout")
    return AuthenticationPost(req);
#endif
  if (!Authenticated(req))
    return Fail(req, "401 Unauthorized", "administrator bearer token required");
  if (system_restart_pending)
    return Fail(req, "409 Conflict", "ESP32 restart pending");
  const std::string_view uri(req->uri);
  const auto work =
      uri == "/api/diagnostics/wire-selftest" ? WebWork::kDetailedDiagnostics : WebWork::kMutation;
  if (!AdmitWebWork(AssessResources(CurrentMemory()), work))
    return ResourceBusy(req);  // Reject before body allocation, parsing or storage/receiver work.
  std::string body, error;
  if (!Body(req, body))
    return Fail(req, "400 Bad Request", "bounded application/json body required");
#if UG_LOCAL_GNSS
  if (uri == "/api/config/portable/preview")
  {
    std::string preview;
    if (!PreviewPortableConfiguration(body, preview, error))
      return Fail(req, "400 Bad Request", error.c_str());
    Headers(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, preview.data(), preview.size());
  }
  if (uri == "/api/config/portable/apply")
  {
    auto root = ParseBoundedObject(body, error);
    if (!root || !OnlyKeys(root.get(), {"confirm", "configuration"}))
      return Fail(req, "400 Bad Request", "explicit portable confirmation required");
    const auto* confirm = cJSON_GetObjectItemCaseSensitive(root.get(), "confirm");
    auto* config = cJSON_GetObjectItemCaseSensitive(root.get(), "configuration");
    if (!cJSON_IsString(confirm) || strcmp(confirm->valuestring, "APPLY_PORTABLE_CONFIGURATION") ||
        !cJSON_IsObject(config))
      return Fail(req, "400 Bad Request", "explicit portable confirmation required");
    char* text = cJSON_PrintUnformatted(config);
    if (!text)
      return Fail(req, "503 Service Unavailable", "memory unavailable");
    const bool ok = ApplyPortableConfiguration(text, error);
    cJSON_free(text);
    if (!ok)
      return Fail(req, "409 Conflict", error.c_str());
    return Fail(req,
                "200 OK",
                "portable settings saved; GNSS disabled; review and restart explicitly; secrets "
                "and pairing retained");
  }
#endif
  if (uri == "/api/radio/pairing/restart")
    return PairingRestartReply(req, body);
  if (uri == "/api/system/reboot")
    return SystemRestartReply(req, body);
  if (uri == "/api/radio/pairing/window" || uri == "/api/radio/pairing/apply" ||
      uri == "/api/radio/pairing/remove")
    return PairingMutation(req, uri, body);
  if (uri == "/api/diagnostics/wire-selftest")
  {
    if (!DecodeWireSelfTest(body))
      return Fail(req, "400 Bad Request", "explicit offline wire self-test confirmation required");
    const auto started = esp_timer_get_time();
    const auto report = RunWireSelfTest();
    const auto elapsed = esp_timer_get_time() - started;
    JsonDocument root(cJSON_CreateObject(), cJSON_Delete);
    if (!root || !cJSON_AddStringToObject(root.get(), "scope", "offline_private_codec_buffers") ||
        !cJSON_AddBoolToObject(root.get(), "ok", report.ok()) ||
        !cJSON_AddNumberToObject(root.get(), "failed_mask", report.failed) ||
        !cJSON_AddNumberToObject(root.get(), "elapsed_us", elapsed))
      return Fail(req, "503 Service Unavailable", "memory unavailable");
    auto* checks = cJSON_AddObjectToObject(root.get(), "checks");
    if (!checks)
      return Fail(req, "503 Service Unavailable", "memory unavailable");
    for (unsigned i = 0; i < kWireSelfTestCount; ++i)
      if (!cJSON_AddBoolToObject(checks, WireSelfTestName(i), (report.passed & (1u << i)) != 0))
        return Fail(req, "503 Service Unavailable", "memory unavailable");
    if (!report.ok())
      httpd_resp_set_status(req, "500 Internal Server Error");
    return Json(req, root.get());
  }
  if (uri == "/api/ntrip/restart")
  {
    if (!DecodeNetworkRestart(body, error))
      return Fail(req, "400 Bad Request", error.c_str());
    RestartNetworkServices();
    return Fail(req,
                "202 Accepted",
                "NTRIP restart requested; owners reconnect asynchronously; receiver unchanged");
  }
  if (uri == "/api/tls/object")
  {
    if (!ApplyTlsObject(body, error))
      return Fail(req, "409 Conflict", error.c_str());
    RestartNetworkServices();
    return Fail(req, "200 OK", "TLS object committed; connections restarting");
  }
  if (uri == "/api/config/rollback")
  {
    if (!RollbackConfiguration(body, error))
      return Fail(req, "409 Conflict", error.c_str());
    return Fail(
        req,
        "200 OK",
        "previous gateway settings committed as a new generation; receiver commands are not "
        "undone; admin identity and quarantine retained; boot-only changes require reboot");
  }
  if (uri == "/api/config" || uri == "/config")
  {
    const auto before = Configuration();
    Settings candidate;
    if (!DecodeSettings(body, *before, BoardCapabilities(), candidate, error))
      return Fail(req, "400 Bad Request", error.c_str());
    const bool changed = candidate.radio_peer != before->radio_peer ||
                         candidate.radio_key != before->radio_key ||
                         candidate.radio_channel != before->radio_channel ||
                         candidate.radio_enabled != before->radio_enabled;
    if (changed && !pairing_window.Active(esp_timer_get_time()))
      return Fail(req, "409 Conflict", "open pairing window before radio changes");
    if (!PatchConfiguration(body, error))
      return Fail(req, "409 Conflict", error.c_str());
    if (changed)
    {
      espnow_rtcm_stop_until_reboot();
      pairing_window.Close();
    }
    return Fail(req, "200 OK", "configuration saved; radio/Wi-Fi/TLS changes require reboot");
  }
#if UG_LOCAL_GNSS
  if (uri == "/api/gnss/base/disable")
  {
    BaseDisableOperation operation;
    if (!DecodeBaseDisableOperation(body, operation, error))
      return Fail(req, "400 Bad Request", error.c_str());
    const auto receiver = ReceiverSnapshot();
    if (!receiver.present || !receiver.model_verified || receiver.quarantined ||
        !InspectReceiverPlans(receiver.model, receiver.model_verified).disable_base ||
        !QueueBaseDisableOperation(operation))
      return Fail(req,
                  "409 Conflict",
                  "BASE disable requires an available verified F9P/UM980/UM982, without quarantine "
                  "or pending command");
    return Fail(req,
                "202 Accepted",
                "BASE disable queued; command acceptance does not prove physical stop");
  }
  if (uri == "/api/gnss/detect")
  {
    if (!DecodeDetectionOperation(body, error))
      return Fail(req, "400 Bad Request", error.c_str());
    if (!Configuration()->gnss_enabled || !QueueReceiverDetection())
      return Fail(req, "409 Conflict", "enable the reviewed GNSS UART first; owner may be busy");
    return Fail(req,
                "202 Accepted",
                "read-only detection queued; local corrections may pause; quarantine is preserved");
  }
  if (uri == "/api/gnss/restart")
  {
    if (!DecodeReceiverRestart(body, error))
      return Fail(req, "400 Bad Request", error.c_str());
    const auto receiver = ReceiverSnapshot();
    if (!receiver.present || !receiver.model_verified || receiver.busy || receiver.quarantined ||
        !InspectReceiverPlans(receiver.model, receiver.model_verified).restart ||
        !QueueReceiverRestart())
      return Fail(
          req, "409 Conflict", "restart requires an idle verified ZED-F9P without quarantine");
    return Fail(req, "202 Accepted", "GNSS-only hot restart queued; no reset ACK, follow status");
  }
  if (uri == "/api/gnss/telemetry/apply")
  {
    TelemetryOperation operation;
    if (!DecodeTelemetryOperation(body, operation, error))
      return Fail(req, "400 Bad Request", error.c_str());
    const auto receiver = ReceiverSnapshot();
    if (!receiver.present || !receiver.model_verified || receiver.quarantined)
      return Fail(req, "409 Conflict", "receiver not verified or quarantined");
    const auto plan = BuildTelemetryPlan(operation, receiver.model, receiver.model_verified);
    if (!plan.ok())
      return Fail(req, "400 Bad Request", plan.error.c_str());
    if (!QueueTelemetryOperation(operation))
      return Fail(req, "409 Conflict", "receiver busy");
    return Fail(req, "202 Accepted", "telemetry profile queued; observe receiver acknowledgement");
  }
  if (uri == "/api/gnss/base/apply-fixed" || uri == "/api/gnss/base/start-survey" ||
      uri == "/api/gnss/base/apply")
  {
    BaseOperation operation;
    if (!DecodeBaseOperation(body, operation, error))
      return Fail(req, "400 Bad Request", error.c_str());
    if ((uri == "/api/gnss/base/apply-fixed" &&
         operation.mode != universal_gnss_driver::BaseMode::kFixed) ||
        (uri == "/api/gnss/base/start-survey" &&
         operation.mode == universal_gnss_driver::BaseMode::kFixed))
      return Fail(req, "400 Bad Request", "route and mode disagree");
    const auto receiver = ReceiverSnapshot();
    if (!receiver.present || !receiver.model_verified || receiver.quarantined ||
        !QueueBaseOperation(operation))
      return Fail(req, "409 Conflict", "receiver not verified, quarantined or busy");
    return Fail(
        req,
        "202 Accepted",
        "operation queued; observe receiver status for acknowledgement and actual survey evidence");
  }
#endif
  return Fail(req, "404 Not Found", "API operation unavailable in this profile");
}
esp_err_t Asset(httpd_req_t* req)
{
  WebMemoryScope memory(req->uri);
  if (req->content_len)
    return Fail(req, "400 Bad Request", "GET body is not supported");
  std::array<char, 513> encoding{};
  const auto length = httpd_req_get_hdr_value_len(req, "Accept-Encoding");
  if (length >= encoding.size())
    return Fail(req, "406 Not Acceptable", "gzip Web representation required");
  const auto header =
      httpd_req_get_hdr_value_str(req, "Accept-Encoding", encoding.data(), encoding.size());
  if (header != ESP_OK && header != ESP_ERR_NOT_FOUND)
    return Fail(req, "400 Bad Request", "invalid encoding preference");
  httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
  if (!AcceptsGzip({encoding.data(), length}, header == ESP_OK))
    return Fail(req, "406 Not Acceptable", "gzip Web representation required");
  const std::string_view uri(req->uri);
  if (uri != "/" && uri != "/app.js" && uri != "/style.css")
    return Fail(req, "404 Not Found", "asset not found");
  Headers(req);
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  if (uri == "/")
  {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(
        req, reinterpret_cast<const char*>(index_start), index_end - index_start);
  }
  if (uri == "/app.js")
  {
    httpd_resp_set_type(req, "application/javascript");
    return httpd_resp_send(req, reinterpret_cast<const char*>(js_start), js_end - js_start);
  }
  if (uri == "/style.css")
  {
    httpd_resp_set_type(req, "text/css");
    return httpd_resp_send(req, reinterpret_cast<const char*>(css_start), css_end - css_start);
  }
  return Fail(req, "404 Not Found", "asset not found");
}
}  // namespace
bool WebUsesHttps()
{
#if CONFIG_UG_WEB_HTTP
  return false;
#else
  return true;
#endif
}
const char* WebTransportName()
{
  return WebUsesHttps() ? "https" : "http";
}
esp_err_t StartWebService()
{
  const auto finish = [](HttpsStartupReason reason, esp_err_t result) {
    return WebUsesHttps() ? FinishHttpsStartup(reason, result) : result;
  };
  if (server)
    return finish(HttpsStartupReason::kServerStartFailed, ESP_ERR_INVALID_STATE);
#if CONFIG_UG_WEB_HTTP
  // Explicit build selection, never entered as a fallback after a TLS error.
  // Keep provisioned identities in NVS; client-side NTRIP TLS is independent.
  httpd_config_t httpd = ug_http_defaults();
#else
#if CONFIG_IDF_TARGET_ESP32S3
  const auto bootstrap = WebIdentityBootStatus();
  if (!bootstrap.ok())
    return FinishHttpsStartup(bootstrap.reason, bootstrap.sdk_error);
#endif
  const auto loaded = LoadWebIdentity(identity);
  if (!loaded.ok())
    return FinishHttpsStartup(loaded.reason, loaded.sdk_error);  // No plaintext fallback.
  httpd_ssl_config_t config = ug_https_defaults();
  auto& httpd = config.httpd;
#endif
#if CONFIG_IDF_TARGET_ESP32S3 && UG_S3_CPU_PROFILE > 0
  httpd.core_id = ug_s3_worker_core(UG_S3_WORKER_WEB);
#endif
  httpd.task_priority = 2;   // Below correction routing (5) and UART/radio owners (4).
  httpd.stack_size = 24576;  // Same baseline for the initial HTTP/HTTPS comparison.
  httpd.max_uri_handlers = 10;
  httpd.max_open_sockets = kHttpsClientLimit;
  httpd.lru_purge_enable = true;
  httpd.recv_wait_timeout = 3;
  httpd.send_wait_timeout = 3;
  httpd.uri_match_fn = httpd_uri_match_wildcard;
#if CONFIG_UG_WEB_HTTP
  auto result = httpd_start(&server, &httpd);
#else
  config.tls_handshake_timeout_ms = 3000;
  config.servercert = reinterpret_cast<const std::uint8_t*>(identity.certificate_pem());
  config.servercert_len = identity.certificate_size + 1;
  config.prvtkey_pem = reinterpret_cast<const std::uint8_t*>(identity.private_key_pem());
  config.prvtkey_len = identity.private_key_size + 1;
  auto result = httpd_ssl_start(&server, &config);
#endif
  if (result != ESP_OK)
  {
    identity = {};
    return finish(HttpsServerFailureReason(result, false), result);
  }
  const struct
  {
    const char* path;
    httpd_method_t method;
    esp_err_t (*handler)(httpd_req_t*);
  } routes[] = {{"/", HTTP_GET, Asset},
                {"/app.js", HTTP_GET, Asset},
                {"/style.css", HTTP_GET, Asset},
                {"/config", HTTP_GET, Get},
                {"/config", HTTP_POST, Post},
                {"/status", HTTP_GET, Get},
                {"/api/*", HTTP_GET, Get},
                {"/api/*", HTTP_POST, Post}};
  for (const auto& route : routes)
  {
    httpd_uri_t uri{};
    uri.uri = route.path;
    uri.method = route.method;
    uri.handler = route.handler;
    if ((result = httpd_register_uri_handler(server, &uri)) != ESP_OK)
    {
      if ((WebUsesHttps() ? httpd_ssl_stop(server) : httpd_stop(server)) == ESP_OK)
      {
        server = nullptr;
        identity = {};
      }
      return finish(HttpsServerFailureReason(result, true), result);
    }
  }
  return finish(HttpsStartupReason::kReady, ESP_OK);
}
}  // namespace ug::embedded
