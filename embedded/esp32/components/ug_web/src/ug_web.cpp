// SPDX-License-Identifier: LGPL-3.0-only
#include "ug_web.hpp"
extern "C" {
#include "espnow_rtcm.h"
}
#include "esp_heap_caps.h"
#include "esp_https_server.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "ug_admin.hpp"
#include "ug_configuration.hpp"
#include "ug_credentials.hpp"
#include "ug_gateway_service.hpp"
#include "ug_json.hpp"
#include "ug_services.hpp"
#include "ug_supervisor.hpp"
#if UG_LOCAL_GNSS
#include "ug_gnss_service.hpp"
#include "universal_gnss_driver/unicore_model_profile.hpp"
#endif
#include <array>
#include <cstring>
#include <string_view>
extern "C" httpd_ssl_config_t ug_https_defaults(void);
namespace ug::embedded
{
namespace
{
WebIdentity identity;
httpd_handle_t server;
extern "C" {
extern const std::uint8_t index_start[] asm("_binary_index_html_start"),
    index_end[] asm("_binary_index_html_end");
extern const std::uint8_t js_start[] asm("_binary_app_js_start"), js_end[] asm(
                                                                      "_binary_app_js_end");
extern const std::uint8_t css_start[] asm("_binary_style_css_start"), css_end[] asm(
                                                                          "_binary_style_css_end");
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
bool Authenticated(httpd_req_t* req)
{
  const auto size = httpd_req_get_hdr_value_len(req, "Authorization");
  if (!size || size > 135)
    return false;
  std::array<char, 136> header{};
  if (httpd_req_get_hdr_value_str(req, "Authorization", header.data(), header.size()) != ESP_OK)
    return false;
  return AuthorizedBearer({header.data(), size}, Configuration()->admin_token_sha256, Sha256);
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
  char* text = root ? cJSON_PrintUnformatted(root) : nullptr;
  if (!text)
    return Fail(req, "503 Service Unavailable", "response memory budget unavailable");
  Headers(req);
  httpd_resp_set_type(req, "application/json");
  const auto result = httpd_resp_sendstr(req, text);
  cJSON_free(text);
  return result;
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
  cJSON_AddBoolToObject(root.get(), "configuration_writable", ConfigurationWritable());
  cJSON_AddBoolToObject(root.get(), "admin_configured", !config->admin_token_sha256.empty());
  cJSON_AddNumberToObject(root.get(), "uptime_s", esp_timer_get_time() / 1000000);
  cJSON_AddNumberToObject(root.get(), "heap_free", esp_get_free_heap_size());
  cJSON_AddNumberToObject(root.get(), "heap_min", esp_get_minimum_free_heap_size());
  cJSON_AddNumberToObject(root.get(),
                          "largest_internal",
                          heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  cJSON_AddBoolToObject(root.get(), "upstream_connected", net.upstream_connected);
  cJSON_AddBoolToObject(root.get(), "upstream_flowing", net.upstream_flowing);
  cJSON_AddNumberToObject(root.get(), "rtcm_frames", gateway.routing.valid);
  cJSON_AddNumberToObject(root.get(), "rtcm_rejected", gateway.routing.rejected);
  cJSON_AddNumberToObject(root.get(),
                          "queue_drops",
                          gateway.ingress_dropped + gateway.destination_dropped +
                              gateway.routing.dropped_full);
  cJSON_AddNumberToObject(root.get(), "stale_drops", gateway.routing.dropped_stale);
  cJSON_AddBoolToObject(root.get(), "receiver_quarantined", ReceiverJournalBlocked());
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
esp_err_t Get(httpd_req_t* req)
{
  if (req->content_len)
    return Fail(req, "400 Bad Request", "GET body is not supported");
  const std::string_view uri(req->uri);
  if (uri == "/api/status" || uri == "/status")
    return Status(req);
  if (uri == "/api/capabilities")
    return CapabilitiesReply(req);
  if (!Authenticated(req))
    return Fail(req, "401 Unauthorized", "administrator bearer token required");
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
  if (uri == "/api/gnss/capabilities")
    return GnssCapabilitiesReply(req);
  if (uri == "/api/gnss/satellites")
    return SatellitesReply(req);
  if (uri == "/api/gnss/status" || uri == "/api/gnss/diagnostics" || uri == "/api/gnss/base/status")
    return GnssReply(req);
#endif
  return Fail(req, "404 Not Found", "API operation unavailable in this profile");
}
esp_err_t Post(httpd_req_t* req)
{
  if (!Authenticated(req))
    return Fail(req, "401 Unauthorized", "administrator bearer token required");
  std::string body, error;
  if (!Body(req, body))
    return Fail(req, "400 Bad Request", "bounded application/json body required");
  const std::string_view uri(req->uri);
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
    if (!PatchConfiguration(body, error))
      return Fail(req, "409 Conflict", error.c_str());
    return Fail(req, "200 OK", "configuration saved; radio/Wi-Fi/TLS changes require reboot");
  }
#if UG_LOCAL_GNSS
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
  if (req->content_len)
    return Fail(req, "400 Bad Request", "GET body is not supported");
  const std::string_view uri(req->uri);
  Headers(req);
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
esp_err_t StartWebService()
{
  std::string error;
  if (!LoadWebIdentity(identity, error))
    return ESP_ERR_INVALID_STATE;  // No plaintext fallback.
  httpd_ssl_config_t config = ug_https_defaults();
  config.httpd.task_priority = 2;  // Below correction routing (5) and UART/radio owners (4).
  config.httpd.stack_size = 24576;
  config.httpd.max_uri_handlers = 10;
  config.httpd.max_open_sockets = 3;
  config.httpd.lru_purge_enable = true;
  config.httpd.recv_wait_timeout = 3;
  config.httpd.send_wait_timeout = 3;
  config.httpd.uri_match_fn = httpd_uri_match_wildcard;
  config.tls_handshake_timeout_ms = 3000;
  config.servercert = reinterpret_cast<const std::uint8_t*>(identity.certificate_pem.c_str());
  config.servercert_len = identity.certificate_pem.size() + 1;
  config.prvtkey_pem = reinterpret_cast<const std::uint8_t*>(identity.private_key_pem.c_str());
  config.prvtkey_len = identity.private_key_pem.size() + 1;
  auto result = httpd_ssl_start(&server, &config);
  if (result != ESP_OK)
    return result;
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
      httpd_ssl_stop(server);
      server = nullptr;
      return result;
    }
  }
  return ESP_OK;
}
}  // namespace ug::embedded
